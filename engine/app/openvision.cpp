// OpenVision — preprocess → inference → fusion → display
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <config/openvision_config.hpp>
#include <common/utils.hpp>
#include <engine/onnx_engine.hpp>
#include <vehicle_interface/vehicle_interface.hpp>
#include <vehicle_interface/can_interface.hpp>
#include <image_preprocessing/image_preprocessor.hpp>
#include <logging/logger.hpp>
#include <models/inference.hpp>
#include <models/depth_analyser.hpp>
#include <planning/planning.hpp>
#include <visualization/visualization.hpp>
#include <debug/debug_draw.hpp>

#include "camera_interface/v4l2_camera_interface.hpp"
#include "camera_interface/file_interface.hpp"
#include "vehicle_interface/file_interface.hpp"

#if ENABLE_ROS2_INTERFACE
#include <rclcpp/rclcpp.hpp>
#include <camera_ros2_interface/camera_ros2_interface.hpp>
#include <vehicle_ros2_interface/vehicle_ros2_interface.hpp>
#endif

namespace ve = openvision::engine;
namespace vm = openvision::models;
namespace vd = openvision::debug;

int main(int argc, char** argv)
{
    Config cfg;
    try { cfg = load_openvision_config(); }
    catch (const std::exception& e)
    {
        VP_ERROR("Config: %s", e.what());
        return 1;
    }

    // ── CLI flags ─────────────────────────────────────────────────────────────
    bool show_window = true;
    bool debug_viz = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--debug-viz") debug_viz = true;
        else if (arg == "--no-window") show_window = false;
    }

    std::shared_ptr<CameraInterface> camera_interface;
    std::shared_ptr<VehicleInterface> vehicle_interface;
#if ENABLE_ROS2_INTERFACE
    rclcpp::init(argc, argv);
    camera_interface = std::make_unique<CameraRos2Interface>(cfg.source.input_camera_topic);
    vehicle_interface = std::make_shared<VehicleRos2Interface>(cfg.vehicle_speed_topic,
                                                               cfg.vehicle_steering_topic,
                                                               cfg.vehicle_acceleration_topic);
#else
    if (cfg.source.mode == SourceMode::Video)
    {
        camera_interface = std::make_unique<camera_interface::FileInterface>(
            cfg.source.input_video, cfg.source.video_loop, cfg.source.video_realtime);
        vehicle_interface = std::make_shared<FileInterface>(cfg.source.input_vehicle_speed);
    }
    else
    {
        camera_interface = std::make_unique<camera_interface::V4L2CameraInterface>(
            cfg.source.v4l2_device, static_cast<uint32_t>(cfg.source.v4l2_fps));
        vehicle_interface = std::make_shared<CanInterface>();
    }
#endif

    ImagePreprocessor preprocessor;
    ve::OnnxEngine engine(cfg.engine);
    vm::InferencePipeline pipeline(engine, cfg.inference);
    Planner planner(cfg.speed_limit, cfg.L);
    if (cfg.rrd_on) logging::Rerun::init(cfg.rrd_log);

    // ── Init visualization assets once based on mode ──────────────────────────
    if (debug_viz)
    {
        VP_INFO("[Viz] Debug mode — annotated telemetry overlay");
        vd::init_wheel_assets(cfg.wheel_dir);
        vd::init_homography();
    }
    else
    {
        VP_INFO("[Viz] Production mode — clean HUD");
        visualization::init_production_assets();
    }

    // ── Initialize camera interface ───────────────────────────────────────────

    if (!camera_interface || !camera_interface->is_device_open())
    {
        VP_ERROR("Cannot open frame source");
        return 1;
    }

    // ── Initialize display ────────────────────────────────────────────────────
    visualization::Visualization visualization({cfg.webrtc_on, cfg.webrtc_port, show_window});

    const cv::Size net_size(vm::AutoDrive::NET_W, vm::AutoDrive::NET_H);
    cv::Mat frame, warped, resized;
    bool h_resized_set = false;
    cv::Mat H = load_matrix("H.yaml", "H");

    // Depth analyser: MiDaS-small monocular depth, refines the flat-ground
    // homography distances of detections so vehicles off the ground plane
    // (bodies, hoods, distant cars) keep their true metric distance.
    vm::DepthAnalyser::Config depth_cfg;
    depth_cfg.model_path = "assets/depth/midas-small.onnx";
    depth_cfg.enabled = cfg.world_log_on;
    vm::DepthAnalyser depth_analyser(engine, depth_cfg);

    // World-model feed: one JSONL line per processed frame.
    std::ofstream world_log;
    if (cfg.world_log_on)
    {
        world_log.open(cfg.world_log_path, std::ios::out | std::ios::trunc);
        if (!world_log.is_open())
        {
            VP_ERROR("Cannot open world log %s", cfg.world_log_path.c_str());
            return 1;
        }
        VP_INFO("[WorldLog] writing per-frame perception to %s", cfg.world_log_path.c_str());
    }
    while (true)
    {
        auto [ok, frame] = camera_interface->get_latest_frame();
        if (!ok || frame.empty())
        {
            if (cfg.source.mode == SourceMode::Video && !cfg.source.video_loop) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        preprocessor.preprocess(frame, warped, resized, net_size);
        cv::Size frame_size = frame.size();
        // One-time: tell the pipeline how to project AutoSteer/AutoSpeed outputs
        // back to world when those networks run on the plain-resized image.
        if (!h_resized_set)
        {
            pipeline.set_H_resized(H, frame_size);
            h_resized_set = true;
        }

        // ── Default frame no inference ────────────────────────────────────────────
        cv::Mat display_frame = resized;

        if (const auto r = pipeline.process(warped, resized))
        {
            pipeline.latency().print();

            const double ego_v = vehicle_interface->read();
            const double cte = r->lateral.cte_m;
            const double epsi = r->lateral.yaw_rad;
            const double kappa = r->lateral.curvature;

            // has_cipo: tracker-based — true only when filter tracks a target
            // closer than D_MAX. cipo_raw_found alone must not gate the planner.
            static constexpr double D_MAX = 150.0;
            const bool has_cipo = r->cipo.valid && r->cipo.distance_m < D_MAX;
            const double cipo_v = has_cipo ? r->cipo.velocity_ms : cfg.speed_limit;
            const double cipo_dist = r->cipo.distance_m;

            const double raw_cte = r->lateral.path_valid
                                       ? static_cast<double>(r->lateral.raw_cte_m)
                                       : cte;
            const Plan plan = planner.compute_plan(
                cte, epsi, kappa, ego_v, has_cipo, cipo_v, cipo_dist);

            VP_INFO(
                "plan: tyre=%.4f rad  accel=%.3f m/s²  |  cte=%.2fm(raw=%.2fm) cte_dot=%+.2fm/s  epsi=%.3f epsi_dot=%+.3frad/s  kappa=%.4f  |  cipo=%s  dist=%.1f m  vel=%+.2f m/s",
                plan.steering.empty() ? 0.0 : plan.steering[1],
                plan.acceleration,
                cte,
                raw_cte,
                r->lateral.cte_rate_mps,
                epsi,
                r->lateral.yaw_rate_rps,
                kappa,
                has_cipo ? "true" : "false",
                cipo_dist,
                r->cipo.velocity_ms);

            vehicle_interface->write(
                plan.steering.empty() ? 0.0 : plan.steering[1],
                plan.acceleration);
            cv::Mat viz;  // output visualization image (empty when viz is off)
            if (cfg.visualization_on)
            {
                if (debug_viz)
                {
                    // annotate_frame() draws inplace
                    viz = cfg.rrd_on ? resized.clone() : resized;
                    vd::visualize(viz, *r, source_label(cfg.source), cfg.wheel_dir, pipeline.H_world2resized());
                    display_frame = viz;
                }
                else
                {
                    display_frame = visualization.build_frame(resized, *r, plan, ego_v, pipeline.H_resized(), cfg.speed_limit);
                    viz = display_frame;
                }
            }

            // Submit all required logging params to single logger func
            if (cfg.rrd_on)
                logging::Rerun::log_frame(r->frame_id, frame, warped, resized, *r, plan, ego_v, viz);

            // ── World-model feed: detections projected to world via H_resized ──
            if (cfg.world_log_on)
            {
                const cv::Mat& Hw = pipeline.H_resized();
                std::string dets = "\"dets\":[";
                if (!Hw.empty())
                {
                    std::vector<cv::Point2f> px, wx;
                    px.reserve(r->auto_speed.detections.size());
                    for (const auto& d : r->auto_speed.detections)
                        px.emplace_back((d.x1 + d.x2) * 0.5f, d.y2);   // bottom-center, like CIPO selection
                    if (!px.empty())
                        cv::perspectiveTransform(px, wx, Hw);

                    // ── Depth refinement: MiDaS + homography fusion per detection ──
                    // dets_px are in resized (net) coordinates — the same image
                    // the detector ran on — and homo_dists are the flat-ground
                    // distances the world model would see without depth.
                    std::vector<float> homo_dists(r->auto_speed.detections.size());
                    for (size_t i = 0; i < r->auto_speed.detections.size(); ++i)
                        homo_dists[i] = static_cast<float>(
                            std::sqrt(wx[i].x * wx[i].x + wx[i].y * wx[i].y));
                    const auto refined = depth_analyser.refine(
                        resized, homo_dists, px, Hw.inv());

                    char buf[160];
                    for (size_t i = 0; i < r->auto_speed.detections.size(); ++i)
                    {
                        const auto& d = r->auto_speed.detections[i];
                        snprintf(buf, sizeof(buf),
                                 "%s{\"cls\":%d,\"score\":%.3f,\"x\":%.2f,\"y\":%.2f,"
                                 "\"d_homo\":%.2f,\"d_depth\":%.2f,\"d\":%.2f,"
                                 "\"d_conf\":%.2f}",
                                 i ? "," : "", d.class_id, d.score, wx[i].x, wx[i].y,
                                 refined[i].raw_distance_m, refined[i].distance_m,
                                 refined[i].distance_m, refined[i].confidence);
                        dets += buf;
                    }
                }
                dets += "]";
                const auto& c = r->cipo;
                const auto& l = r->lateral;
                world_log << "{\"frame_id\":" << r->frame_id
                          << ",\"ego_v\":" << ego_v
                          << ",\"wall_ms\":" << r->wall_ms
                          << ",\"cipo\":{\"valid\":" << (c.valid ? "true" : "false")
                          << ",\"d\":" << c.distance_m
                          << ",\"v\":" << c.velocity_ms
                          << ",\"std\":" << c.distance_stddev_m
                          << ",\"raw_found\":" << (c.cipo_raw_found ? "true" : "false")
                          << ",\"cut_in\":" << (c.cut_in_detected ? "true" : "false")
                          << "},\"path\":{\"valid\":" << (l.path_valid ? "true" : "false")
                          << ",\"a\":" << l.path_a
                          << ",\"b\":" << l.path_b
                          << ",\"c\":" << l.path_c
                          << ",\"x_min\":" << l.path_x_min_m
                          << ",\"cte\":" << l.cte_m
                          << ",\"yaw\":" << l.yaw_rad
                          << ",\"kappa\":" << l.curvature
                          << "}," << dets << "}\n";
            }
        }
        if (cfg.visualization_on)
        {
            visualization.render_frame(display_frame);
        }
    }

    if (cfg.rrd_on) logging::Rerun::shutdown();  // flush & close .rrd
    if (cfg.world_log_on) world_log.flush();

    // stop() returns true on a clean shutdown; translate that to a 0 exit code
    // so OpenVision can be supervised as a batch/oneshot job (a successful run
    // must not exit non-zero).
    return visualization.stop() ? 0 : 1;
}
