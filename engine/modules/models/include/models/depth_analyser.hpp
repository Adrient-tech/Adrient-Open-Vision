#pragma once

#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openvision { namespace engine { class OnnxEngine; } }  // fwd decl

namespace openvision::models {

// ─── DepthAnalyser ─────────────────────────────────────────────────────────────
//
// Monocular depth refinement for the perception feed. Runs MiDaS-small (ONNX,
// 256×256 RGB, relative inverse-depth output) on every frame and converts each
// detection's flat-ground homography distance into a metric distance that also
// holds for object surfaces off the ground plane (bumpers, hoods, vehicle
// bodies above the road), where the homography alone is systematically biased.
//
// Calibration: MiDaS produces *relative* inverse depth only. Every frame the
// analyser takes the median MiDaS inverse-depth prediction over high-confidence
// ground pixels (the lower third of the image, where the homography is most
// trustworthy) and compares it with the median homography depth over the same
// pixels, fitting one EMA-smoothed scale that converts MiDaS relative inverse
// depth into metres. Detections whose homography distance already agrees with
// the depth-model prediction (true ground-plane objects, near field) are left
// untouched; everything else gets the depth-model distance.

struct DepthRefinement {
    float distance_m     = 0.0f;  // refined metric distance (forward, ego frame)
    float raw_distance_m = 0.0f;  // original homography distance
    float confidence     = 0.0f;  // 0..1 — agreement between the two estimates
    bool  refined        = false; // true when the depth model moved the estimate
};

class DepthAnalyser {
public:
    struct Config {
        std::string model_path     = "weights/midas-small.onnx";
        int         net_size       = 256;    // MiDaS input is fixed 256×256
        float       max_refine_delta = 0.35f; // max relative change per detection
        float       ema_alpha      = 0.10f;  // calibration smoothing factor
        float       min_distance_m = 1.0f;   // homography trust window (near field)
        float       max_distance_m = 80.0f;  // beyond this homography is too noisy
        bool        enabled        = true;
    };

    DepthAnalyser(openvision::engine::OnnxEngine& engine, const Config& cfg);

    // Compute per-detection refined distances.
    //   frame       : BGR image (any size; resized to net_size internally)
    //   homo_dists  : per-detection flat-ground homography distance [m]
    //   dets_px     : matching pixel positions in `frame` (same order)
    //   H_homo      : pixel → ground-plane homography used for the raw distances
    // Returns one DepthRefinement per input detection, same order.
    std::vector<DepthRefinement> refine(const cv::Mat& frame,
                                        const std::vector<float>& homo_dists,
                                        const std::vector<cv::Point2f>& dets_px,
                                        const cv::Mat& H_homo);

    // EMA-smoothed calibration state (for logging / debug overlay).
    float    scale() const { return scale_ema_; }
    bool     calibrated() const { return frames_seen_ > 0; }
    uint64_t frames_seen() const { return frames_seen_; }

private:
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo mem_info_{nullptr};
    Config cfg_;
    std::vector<const char*> in_names_;
    std::vector<const char*> out_names_;
    std::vector<std::string> in_name_strs_;
    std::vector<std::string> out_name_strs_;

    float    scale_ema_   = 1.0f;
    uint64_t frames_seen_ = 0;
};

}  // namespace openvision::models
