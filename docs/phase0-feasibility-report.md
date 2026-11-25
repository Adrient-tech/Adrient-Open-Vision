# Phase 0 — Hardware and Repository Inspection Report

Date: 2026-11-24 · Environment: Ubuntu Linux cloud workspace, no GPU

## 1. Target machine

| Item | Value |
|---|---|
| OS | Ubuntu 26.04.1 LTS (x86_64) |
| CPU | Intel Xeon @ 2.50 GHz, 8 vCPUs |
| RAM | 16 GiB (15 GiB available, no swap) |
| Disk | 51 GiB free |
| GPU | None (pure CPU target — matches PRD requirement) |
| Toolchain | gcc 15.2.0, cmake 4.2.3, Python 3.14 |

## 2. Upstream repository, pinned

- Repo: https://github.com/autowarefoundation/vision_pilot
- Pinned tag: **v1.2** (checked out at commit `9a96c3e7`), cloned to `perception/`
- Licence: Apache 2.0 (`LICENSE` at repo root). Model weights are bundled in-repo under the same project (see §6).
- Note: the PRD's URL `autowarefoundation/vision_pilot` redirects from the older `autoware_vision_pilot` name used in the upstream README; both resolve to the same repository.

## 3. Build system findings (verified against v1.2 source, not just docs)

- Root `VisionPilot/CMakeLists.txt`:
  - `option(GPU ... ON)` — **GPU is ON by default; `-DGPU=OFF` is required** and adds a `_cpu` package suffix, no CUDA deps.
  - `ENABLE_ROS2_INTERFACE` default OFF — not needed.
  - `ENABLE_RERUN` default OFF — not needed for Phase 1.
- Occupancy BEV: `VisionPilot/modules/visualization/CMakeLists.txt:4` defines `option(ENABLE_OCCUPANCY "Build heuristic Occupancy BEV module" OFF)`. It compiles `src/occupancy_view.cpp` / `src/occupancy_bridge.cpp` and defines `ENABLE_OCCUPANCY=1`. Confirmed it is a heuristic geometric BEV (matches the PRD's "important distinction").
- Model weights are **bundled in the repo** (`VisionPilot/modules/models/weights/`): `autospeed`, `autosteer`, `autodrive` in fp32 and int8 ONNX (~143 MB total). No separate weight download is needed. Int8 weights exist, satisfying the PRD's "use compatible INT8 weights if provided upstream" (switch via `model.precision = int8`).
- Build deps observed in CMake: OpenCV, GStreamer 1.0 (+app/sdp/webrtc), Boost ≥1.70 (system), nlohmann-json, Threads, Python 3 (used at build time to generate `homography_C_matrix.yaml`).

## 4. Real perception output interfaces (Phase 2 adapter targets)

Inspected source headers (these are actual v1.2 structures, not invented):

- `modules/common/include/common/types.hpp` — `struct Plan`, `enum class Warning`.
- `modules/safety_guardian/fusion/include/fusion/lateral_fusion.hpp` — `LateralFusionEstimate` (ego-path/lane estimate; world points documented as `x = forward [m], y = lateral [m, +left]`), plus `WorldPt`, `PathParams` types.
- `modules/safety_guardian/fusion/include/fusion/longitudinal_fusion.hpp` — `CIPOFusionEstimate` (closest in-path object: distance_m, velocity, cut-in flag).
- Model level: `modules/models/src/auto_speed.cpp` (in-path object detection), `auto_steer.cpp` (ego path waypoints), `auto_drive.cpp` (distance / in-path presence / road curvature). Inference via `engine/onnx_engine.hpp` (ONNX Runtime).
- Visualisation pipeline: `modules/visualization/src/visualization.cpp`, `local_display.cpp`, optional `occupancy_view.cpp`; optional WebRTC streaming (`webrtc_on` config, port 8080) — a likely integration point for a remote viewer later.

## 5. Runtime configuration (v1.2)

`VisionPilot/config/vision_pilot.conf` — key keys for Phase 1:
`source.mode = video`, `source.input_video`, `source.input_vehicle_speed` (in `vision_pilot_test.conf`), `engine.provider` (default `cuda` — **must be set to `cpu`**), `model.precision` (fp32|int8), `visualization_on`.

## 6. Licences and dependencies to record

- Vision Pilot: Apache 2.0.
- Dependencies pulled in: OpenCV (Apache 2.0), ONNX Runtime (MIT), GStreamer (LGPL), Boost (BSL), nlohmann/json (MIT), GStreamer WebRTC (LGPL). Full third-party audit is a Phase 6 deliverable; none block building/running locally.
- Sample OpenLane dataset comes from upstream's Google Drive folder; it is derived from OpenLane data and used only locally for evaluation, not redistributed.

## 7. Feasibility conclusion

**GO.** v1.2 builds CPU-only with bundled weights, exposes the perception structures the PRD's adapter expects, and this machine (8 cores, 16 GB) meets the documented Ubuntu 22.04-class requirements. Risks:
1. This is Ubuntu 26.04, not 22.04 — newer GCC may raise warnings/errors; mitigate with `-j2` and, if needed, flag tweaks rather than patching upstream.
2. Headless machine: the HUD/BEV window needs a display — plan to run under Xvfb (or `webrtc_on` streaming) and screenshot to verify HUD and BEV.
3. Sample dataset is on Google Drive — download may need `gdown`.
