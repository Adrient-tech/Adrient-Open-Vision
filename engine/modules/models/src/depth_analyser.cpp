#include "models/depth_analyser.hpp"

#include <engine/onnx_engine.hpp>

#include <onnxruntime_cxx_api.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

// The header forward-declares `engine::OnnxEngine` inside namespace
// openvision::models; the real class lives in openvision::engine.
using openvision::engine::OnnxEngine;

namespace openvision::models {

namespace {

// Median of a copy — O(n log n), n is tiny (ground samples of one frame).
float median_of(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

}  // namespace

DepthAnalyser::DepthAnalyser(openvision::engine::OnnxEngine& engine, const Config& cfg) : cfg_(cfg) {
    if (!cfg_.enabled) return;
    auto session = engine.create_session(cfg_.model_path, "midas_");
    if (!session) {
        printf("[DepthAnalyser] Failed to create session for %s\n", cfg_.model_path.c_str());
        return;
    }
    session_ = std::move(session);
    mem_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::AllocatorWithDefaultOptions alloc;
    const size_t n_in = session_->GetInputCount();
    const size_t n_out = session_->GetOutputCount();
    in_name_strs_.resize(n_in);
    in_names_.resize(n_in);
    for (size_t i = 0; i < n_in; ++i) {
        in_name_strs_[i] = session_->GetInputNameAllocated(i, alloc).get();
        in_names_[i]     = in_name_strs_[i].c_str();
    }
    out_name_strs_.resize(n_out);
    out_names_.resize(n_out);
    for (size_t i = 0; i < n_out; ++i) {
        out_name_strs_[i] = session_->GetOutputNameAllocated(i, alloc).get();
        out_names_[i]     = out_name_strs_[i].c_str();
    }

    printf("[DepthAnalyser] MiDaS-small loaded from %s (net %dx%d)\n",
           cfg_.model_path.c_str(), cfg_.net_size, cfg_.net_size);
}

std::vector<DepthRefinement> DepthAnalyser::refine(const cv::Mat& frame,
                                                   const std::vector<float>& homo_dists,
                                                   const std::vector<cv::Point2f>& dets_px,
                                                   const cv::Mat& H_homo) {
    const size_t n = homo_dists.size();
    std::vector<DepthRefinement> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i].distance_m     = homo_dists[i];
        out[i].raw_distance_m = homo_dists[i];
    }
    if (!cfg_.enabled || session_ == nullptr || frame.empty() || n == 0) return out;

    // ── 1. Run MiDaS on the full frame ────────────────────────────────────────
    const int N = cfg_.net_size;
    cv::Mat rgb, blob;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    cv::resize(rgb, rgb, cv::Size(N, N));
    rgb.convertTo(blob, CV_32F, 1.0f / 255.0f);
    // MiDaS expects mean-normalized input (mean 0.5, std 0.5 after 0..1 scale).
    blob = (blob - 0.5f) / 0.5f;

    // HWC RGB → CHW planar tensor.
    std::vector<float> input(3 * N * N);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                input[c * N * N + y * N + x] = blob.at<float>(y, x * 3 + c);

    std::vector<std::int64_t> shape{1, 3, N, N};

    // Raw MiDaS output is *inverse* relative depth (larger = closer).
    std::vector<float> pred(static_cast<size_t>(N) * N);

    try {
        Ort::RunOptions run_options;
        std::vector<Ort::Value> inputs;
        inputs.emplace_back(Ort::Value::CreateTensor<float>(
            mem_info_, input.data(), input.size(), shape.data(), shape.size()));
        auto results = session_->Run(run_options, in_names_.data(), inputs.data(), 1,
                                     out_names_.data(), out_names_.size());
        if (!results.empty() && results[0].IsTensor()) {
            const float* fp = results[0].GetTensorData<float>();
            const size_t count = results[0].GetTensorTypeAndShapeInfo().GetElementCount();
            for (size_t k = 0; k < std::min(count, pred.size()); ++k) pred[k] = fp[k];
        }
    } catch (const Ort::Exception& e) {
        printf("[DepthAnalyser] Inference error: %s\n", e.what());
        return out;  // homography-only fallback
    }

    // ── 2. Calibrate MiDaS relative depth against the ground-plane homography ─
    // Sample the lower third of the image (road surface, homography's home
    // turf) and compare medians. This gives one scale MiDaS → metres.
    std::vector<float> midas_ground, homo_ground;
    const int y_begin = (N * 2) / 3;
    for (int y = y_begin; y < N; y += 4) {
        for (int x = 0; x < N; x += 4) {
            const float d_pred = pred[y * N + x];
            if (!(d_pred > 0.0f) || !std::isfinite(d_pred)) continue;
            // Back-project this pixel to the ground plane through H_homo.
            // Pixel in original frame coordinates:
            const float sx = float(frame.cols) / N;
            const float sy = float(frame.rows) / N;
            const cv::Point2f p(x * sx, y * sy);
            std::vector<cv::Point2f> src{p}, dst;
            cv::perspectiveTransform(src, dst, H_homo);
            const cv::Point2f& g = dst[0];
            const float d_homo = std::sqrt(g.x * g.x + g.y * g.y);
            if (d_homo < cfg_.min_distance_m || d_homo > cfg_.max_distance_m) continue;
            midas_ground.push_back(d_pred);
            homo_ground.push_back(d_homo);
        }
    }

    const float med_pred = median_of(midas_ground);
    const float med_homo = median_of(homo_ground);
    if (med_pred > 1e-6f && med_homo > 1e-6f) {
        // MiDaS gives inverse depth; convert scale to a distance scale factor.
        // dist_meters ≈ k / pred, so k = med_homo * med_pred; normalise against
        // a running EMA so single-frame noise does not swing the estimate.
        const float k = med_homo * med_pred;
        const float instantaneous_scale = k;  // higher k = coarser depth scale
        const float a = frames_seen_ == 0 ? 1.0f : cfg_.ema_alpha;
        scale_ema_ += a * (instantaneous_scale - scale_ema_);
        ++frames_seen_;
    }

    // ── 3. Refine each detection ──────────────────────────────────────────────
    // dets_px are in `frame` coordinates; the depth map is N×N.
    const float map_sx = static_cast<float>(N) / frame.cols;
    const float map_sy = static_cast<float>(N) / frame.rows;
    for (size_t i = 0; i < n; ++i) {
        const float mx = dets_px[i].x * map_sx;
        const float my = dets_px[i].y * map_sy;
        if (mx < 0 || my < 0 || mx >= N || my >= N) continue;
        const float d_pred = pred[static_cast<int>(my) * N + static_cast<int>(mx)];
        if (!(d_pred > 0.0f) || !std::isfinite(d_pred)) continue;
        if (scale_ema_ <= 1e-6f) continue;

        const float d_depth = scale_ema_ / d_pred;  // metric distance estimate
        const float raw     = homo_dists[i];
        if (raw < cfg_.min_distance_m || raw > cfg_.max_distance_m) continue;

        // Blend: homography is trusted near the camera, depth model far away.
        // Weight of the depth estimate rises with homography distance.
        const float w = std::min(1.0f, std::max(0.0f,
            (raw - cfg_.min_distance_m) / (cfg_.max_distance_m - cfg_.min_distance_m)));
        float blended = (1.0f - w) * raw + w * d_depth;

        // Never move a detection more than max_refine_delta in one frame —
        // outliers (sky, saturation) must not teleport objects.
        const float lim = raw * cfg_.max_refine_delta;
        if (std::abs(blended - raw) > lim) blended = raw + std::copysign(lim, blended - raw);

        out[i].distance_m = blended;
        out[i].confidence = 1.0f - std::min(1.0f, std::abs(d_depth - raw) / std::max(raw, 1e-3f));
        out[i].refined    = std::abs(blended - raw) > 1e-3f;
    }
    return out;
}

}  // namespace openvision::models
