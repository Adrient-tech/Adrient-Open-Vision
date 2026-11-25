# Stage 1 — Streaming the Engine's World View

Date: 2026-11-25 (updated after root-cause fix). Builds on Phase 1 (`docs/phase1-cpu-build-results.md`); roadmap in `docs/simulation-roadmap.md`.

## What was wired and verified

- Rebuilt with `-DENABLE_RERUN=ON` (vendored Arrow/mimalloc needed `CMAKE_POLICY_VERSION_MINIMUM=3.5` for CMake 4).
- Config: `rrd_on = true`, `webrtc_on = true`, `webrtc_port = 8080` in `build/config/vision_pilot.conf`.
- **Rerun**: full run (exit 0) produced `build/visionpilot.rrd` (1.5 GB). View with `rerun visionpilot.rrd`.
- **WebRTC live HUD stream — now working.** Verified on a live run: `[WebRTCStreamer] pipeline set to PLAYING`, port 8080 LISTEN, `curl http://127.0.0.1:8080/` → 200 with the viewer page (4,207 bytes), and WebSocket signaling endpoint `/ws` upgrades correctly (HTTP 101). Open the same URL in a normal browser to watch the HUD stream live.

## Root cause of the earlier "WebRTC defect" (not an upstream code bug)

`gst_element_set_state(PLAYING)` failed because three GStreamer plugins were missing on this machine: `vp8enc`/`rtpvp8pay` (gstreamer1.0-plugins-good), `webrtcbin` (plugins-bad) and, the subtle one, the ICE element `nice` (**`gstreamer1.0-nice`** — a separate package that plugins-bad does not pull in). `gst_parse_launch` still "succeeded" with a half-built pipeline, so the failure surfaced only as a silent state-change failure. Fix:

```
sudo apt-get install -y gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-nice
```

Diagnostic tip for future state-change failures: run with `GST_DEBUG=4` — webrtcbin posts a precise "libnice elements are not available" error before the generic failure.

## How to reproduce

```
cd engine/upstream/VisionPilot/build
cmake .. -DGPU=OFF -DENABLE_OCCUPANCY=ON -DENABLE_RERUN=ON \
  -DONNXRUNTIME_ROOT=/vercel/sandbox/repo/engine/onnxruntime-cpu -DCMAKE_BUILD_TYPE=Release
CMAKE_POLICY_VERSION_MINIMUM=3.5 cmake --build . -j8
LD_LIBRARY_PATH=../../../onnxruntime-cpu/lib xvfb-run -a ./VisionPilot
# → visionpilot.rrd in the build dir; live HUD at http://<host>:8080/
```

## CARLA assessment (stage 3)

CARLA is the right choice for closed-loop simulation — open source (MIT), realistic sensor simulation with ground truth, and a Python API that fits the adapter planned in the PRD. Two caveats on this hardware: it wants a GPU for rendering (software rendering on this 8-core box will be slow, though headless "serverless" mode may be usable for low-res sensor logs), and Vision Pilot consumes plain camera frames + speed, so a CARLA ROS-free sensor shim (Python: spawn camera → PNG + speed txt in the existing `test_open_lane_*` format) is the cheapest integration path — no ROS required.
