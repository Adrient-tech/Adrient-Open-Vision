# Adrient OpenVision

Turn live or recorded camera feeds into a real-time 3D world view.
CPU-first camera perception, multi-object tracking and a browser-based 3D
scene viewer — one self-contained open-source project.

**Website:** [adrient.com](https://adrient.com)

> OpenVision's design is inspired by Tesla's vision-first vehicle software —
> a camera-only perception stack that understands lanes, vehicles and the
> ego path without radar or lidar.

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

## Feeding input

All input settings live in `engine/config/openvision.conf` under the
`source.*` keys. Three input modes are supported:

### 1. Video file / recorded dataset (default)

```ini
source.mode             = video
source.input_video         = /path/to/driving.mp4
source.input_vehicle_speed = /path/to/frame_speed.txt   # one speed per frame, m/s
source.video_realtime      = true    # true = play at capture rate, false = as fast as possible
source.video_loop          = true
```

A speed file is required for `video` mode; if you don't have one, generate a
constant-speed file (e.g. one `8.0` line per frame of the video).

Or just point the one-command pipeline at your own footage:

```bash
scripts/run_pipeline.sh /path/to/driving.mp4 10    # second arg is fps
```

### 2. Single live camera (webcam / USB / V4L2 device)

```ini
source.mode        = v4l2
source.v4l2_device = /dev/video0
source.v4l2_fps    = 10
```

Then run the engine from the build directory:

```bash
cd engine/build
LD_LIBRARY_PATH=../../third_party/onnxruntime-cpu/lib xvfb-run -a ./OpenVision
cd ../..
(cd worldmodel && cargo run --release --bin worldmodel -- \
    ../engine/build/perception.jsonl ../engine/build/world_state.jsonl 10)
(cd worldmodel && ./target/release/adapter \
    ../engine/build/world_state.jsonl ../viewer/scene.json 10)
```

### 3. Multiple cameras / ROS 2 camera topics

Each engine instance consumes one camera. To run several cameras, run one
instance per camera with its own config, device (or ROS 2 topic) and
`world_log_path`, then feed each produced `world_state.jsonl` to the world
model and merge the scenes with the adapter:

```ini
source.mode               = ros2
source.input_camera_topic = /camera/front/image
world_log_path            = perception_front.jsonl
```

### 4. 3D recording / simulation view

Set `rrd_on = true` to stream every frame into an `.rrd` recording you can
open interactively with [Rerun](https://rerun.io):

```bash
rrd_on = true
rrd_log = openvision.rrd
# open with: rerun openvision.rrd
```

## Camera setup

A forward-facing camera behind the windscreen, under the rear-view mirror,
works best: 2 MP or higher, horizontal field of view between 50 and 55
degrees, zero roll, facing forward with a 1–3 degree downward pitch
(10–15 degrees for tall vehicles). See `engine/Sensing/README.md` and the
calibration guide in `engine/Calibration/` for the full procedure.
Full rig guides — single dashcam, multi-camera ring, BEV type: **[docs/camera-setup.md](docs/camera-setup.md)**.

## Models

All models are ONNX and ship pre-trained (YOLO11n detector with 80 COCO
classes — car, truck, bus, motorcycle, bicycle, person, traffic light, stop
sign — plus MiDaS-small depth). Fetch them with `models/download_models.sh`;
details and the retraining guide: **[models/README.md](models/README.md)** and
**[docs/training.md](docs/training.md)**.

## 3D reconstruction (video → geometry)

`scripts/reconstruct3d.py` converts the dashcam video into actual 3D geometry,
the way Tesla's render does: every frame's MiDaS depth is calibrated against
the ground-plane homography (`engine/config/H.yaml`), back-projected to metric
3D, and fused across frames using the ego odometry from `world_state.jsonl`:

```bash
python3 scripts/reconstruct3d.py <input.mp4> <world_state.jsonl> \
    engine/config/H.yaml engine/assets/depth/midas-small.onnx viewer/assets
```

It writes `viewer/assets/reconstruction.{bin,json}`; the viewer's
**Reconstruction** toggle renders the fused, coloured point cloud in the same
world space as the tracked boxes (the wireframe grid hides while it is on).

## Documentation

| Page | Contents |
|---|---|
| [docs/camera-setup.md](docs/camera-setup.md) | Camera setup: single dashcam, multi-camera ring, BEV type, calibration, troubleshooting |
| [docs/cpu-setup.md](docs/cpu-setup.md) | CPU-only build and run, performance tuning |
| [docs/gpu-setup.md](docs/gpu-setup.md) | CPU + GPU setup with the CUDA ONNX Runtime, remote GPU hosts |
| [docs/training.md](docs/training.md) | Training / fine-tuning the detector, lane head and BEV network |
| [docs/world-model.md](docs/world-model.md) | Tracker internals: gates, confirmation, CIPO |
| [docs/build-and-benchmarks.md](docs/build-and-benchmarks.md) | Build details and measured benchmark numbers |
| [models/README.md](models/README.md) | Model inventory, downloads, export commands |

## Configuration reference

| Key | Default | Meaning |
|---|---|---|
| `source.mode` | `video` | input source (`video`, `v4l2` or `ros2`) |
| `engine.provider` | `cpu` | ONNX Runtime execution provider |
| `source.video_realtime` | `true` | run at capture rate or as fast as possible |
| `world_log_on` | `true` | write per-frame perception log for the world model |
| `rrd_on` | `false` | stream to a 3D recording (`*.rrd`) |

## Testing

```bash
# End-to-end: build → perception → world model → adapter on the sample dataset
scripts/run_pipeline.sh

# Validate the generated scene file
python3 -c "import json; v=json.load(open('viewer/scene.json')); print(len(v['frames']), 'frames')"

# World model unit tests
cd worldmodel && cargo test
```

Then open the viewer (`python3 -m http.server 3000 --directory viewer`) and
check that vehicles are tracked, the ego path is drawn and the HUD shows a
lead-vehicle distance.

## Requirements

- Linux, 8+ CPU cores (no GPU required)
- CMake ≥ 3.16, GCC/Clang, OpenCV, GStreamer, Python 3
- Rust toolchain (world model)

## Licence

Apache-2.0 — see `LICENSE`.
