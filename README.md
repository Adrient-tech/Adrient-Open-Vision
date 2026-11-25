# Adrient OpenVision

Turn live or recorded camera feeds into a real-time 3D world view.
CPU-first camera perception, multi-object tracking and a browser-based 3D
scene viewer — one self-contained open-source project.

![status](https://img.shields.io/badge/build-passing-brightgreen)

## What it does

1. **Perception** (`engine/`) — C++ camera pipeline: lane planning, cross-track
   error, lead-vehicle detection, occupancy grid, running on CPU via ONNX
   Runtime at interactive frame rates.
2. **World model** (`worldmodel/`) — Rust tracker that turns raw per-frame
   detections into persistent objects (confirmed/tentative/trail states),
   fits the ego path, and tracks the closest in-path object (CIPO).
3. **Scene adapter** (`worldmodel/src/bin/adapter.rs`) — converts the world
   model output into a compact `scene.json` (`adrient.openvision/scene@1`).
4. **Viewer** (`viewer/`) — branded 3D web viewer: tracked vehicles, ego path,
   HUD with speed / lead distance / cut-in warnings, play + scrub controls.

## Repository layout

```
engine/       C++ perception engine + models (ONNX Runtime, CPU)
worldmodel/   Rust world model and scene adapter
viewer/       3D web viewer (Three.js)
third_party/  ONNX Runtime build, CMake helper modules
data/         sample driving sequences for offline runs
docs/         setup, configuration and architecture documentation
```

## Quick start

```bash
# Build and run the full pipeline (first run compiles the engine)
scripts/run_pipeline.sh

# Serve the viewer
python3 -m http.server 3000 --directory viewer
# open http://localhost:3000
```

## Configuration

All runtime settings live in `engine/config/openvision.conf`:

| Key | Default | Meaning |
|---|---|---|
| `source.mode` | `video` | input source (`video` or `v4l2`) |
| `engine.provider` | `cpu` | ONNX Runtime execution provider |
| `source.video_realtime` | `false` | run at capture rate or as fast as possible |
| `world_log_on` | `true` | write per-frame perception log for the world model |
| `rrd_on` | `false` | stream to a 3D recording (`*.rrd`) |

The pipeline script writes `engine/config/openvision_test.conf` with the
sample dataset paths; point `source.input_video` and
`source.input_vehicle_speed` at your own driving footage to test with
different videos.

## Requirements

- Linux, 8+ CPU cores (no GPU required)
- CMake ≥ 3.16, GCC/Clang, OpenCV, GStreamer, Python 3
- Rust toolchain (world model)

## Licence

Apache-2.0 — see `LICENSE`.
