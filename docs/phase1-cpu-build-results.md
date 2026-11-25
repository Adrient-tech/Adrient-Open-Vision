# Phase 1 — CPU Build and Recorded-Video Run Results

Date: 2026-11-24 · Vision Pilot v1.2 (commit 9a96c3e7) · CPU-only
Context: this benchmark closes the open item from the same session — see also `docs/simulation-roadmap.md` for where the throughput numbers feed the staged simulation plan (Tesla's Reality View comparison: [Dealroom](https://dealroom.co/news/x-2124461034182197621-musk-explains-the-diffusion-world-model-behind-teslas-new-fsd-reality-vi/), [Embodied AI 101](https://embodied.transistor.fm/episodes/tesla-world-models-what-do-gaussian-splatting-and-meanflow-deliver/transcript), [AI Shift News](https://aishiftnews.ca/news/tesla-shows-optimus-walking-inside-a-world-model-at-corl/), [Robotics Weekly](https://roboticsweekly.news/issues/2026-11-22)).

## Build

- Configure: `cmake .. -DGPU=OFF -DENABLE_OCCUPANCY=ON -DONNXRUNTIME_ROOT=third_party/onnxruntime-cpu -DCMAKE_BUILD_TYPE=Release`
- Result: build completes 100% (`Built target VisionPilot`).
- Host deltas vs upstream docs (Ubuntu 26.04, not 22.04):
  - Extra apt packages beyond upstream list: `coinor-libipopt-dev`, `libcppad-dev`, `python3-opencv` (Python 3.14 has no cv2 wheel; the apt package provides it for build-time homography generation).
  - Boost 1.90 ships no `boost_system` CMake config (header-only since 1.69); a local stub lives at `third_party/boost-cmake/boost_system-1.90.0/` (added via `CMAKE_PREFIX_PATH`, no upstream source change).
  - ONNX Runtime: assembled from the PyPI wheel `onnxruntime==1.24.1` (lib) plus headers from the matching v1.24.1 tag; layout `third_party/onnxruntime-cpu/{include,lib}`. Run binaries with `LD_LIBRARY_PATH=third_party/onnxruntime-cpu/lib`.
  - One-line source change: uncommented `pipeline.latency().print()` in `app/vision_pilot.cpp` to expose per-frame latency (needed for the PRD's measurement mandate).

## Dataset

OpenLane sample sequences from upstream's Google Drive, downloaded to `data/sample_dataset/` (test_open_lane_1..8, each `input.mp4` + `frame_speed.txt`). Benchmark used `test_open_lane_2` (1,198 frames @ 10 fps).

## Run (recorded-video mode, config `config/vision_pilot_test.conf`)

`source.video_realtime=false` (max speed), `engine.provider=cpu`, model precision fp32, visualization ON, Occupancy BEV ON.

### Measured performance (8 vCPU Xeon @ 2.50 GHz)

| Metric | fp32 | int8 |
|---|---|---|
| Wall time for 1,198 frames | 58.6 s | 76.4 s |
| Throughput (per-frame latency print) | ~4 fps (wall ≈ 250–270 ms/frame) | ~3 fps (wall ≈ 302–375 ms/frame) |
| Latency breakdown (typical) | pre 12.6 ms, AutoDrive 240 ms, AutoSteer 195 ms, AutoSpeed 150 ms (parallel ≈ 240 ms), wall 253 ms | pre 12.7 ms, AutoDrive 290–360 ms, AutoSteer 150–274 ms, AutoSpeed 235–341 ms (parallel ≈ 290–361 ms), wall ≈ 354 ms |
| CPU utilisation | 656% of 800% (~6.6 cores busy) | 651% |
| Peak RSS | 651,836 kB (~636 MiB) | 625,092 kB (~611 MiB) |

**Surprising result: int8 is *slower* than fp32 on this Xeon (3 vs 4 fps).** Plausible causes: the Xeon (no VNNI-era low-power path engaged through this ONNX Runtime build) plus QDQ-style graph overhead; also note the int8 run was 1,198 of 1,198 frames (197 plan logs, exit 0), so it is a full-run comparison, not a warm-up artefact. Keep `model.precision = fp32` as the default on CPU.

### Correctness

- 197 plan/fusion log lines over the clip; stable ego-path estimates (`cte` within ±0.4 m, `kappa` ~1e-4), CIPO acquisition and target-change re-init logged with plausible distances (149→330 m gate behaviour).
- Visual check (Xvfb screenshot): HUD renders the drivable corridor, vehicle bounding boxes, lead-vehicle distance (119 m) and speed limit; the Occupancy BEV window renders the lane corridor on the occupancy grid. Both confirmed in `/tmp/hud.png` during the run.

## Verdict

The perception engine works on the actual CPU target: end-to-end inference + fusion + HUD/BEV at ~4 fps fp32, 6.6 cores, under 700 MiB RAM. int8 was benchmarked and is slower on this CPU (3 fps), so fp32 stays the default; the honest next levers are threading tuning (ORT `intra_op` threads), a newer ORT build with XNNPACK/oneDNN EPs, or a GPU for the heavy models.
