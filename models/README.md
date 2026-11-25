# Models

Every neural network Adrient OpenVision runs, all ONNX so the same files work
on CPU and (with the CUDA build of ONNX Runtime) GPU.

| File | Task | Input | Classes / output |
|---|---|---|---|
| `modules/models/weights/yolo11n.onnx` | Object detection (YOLO11n) | 1×3×512×1024 | 80 COCO classes: car, person, truck, bus, motorcycle, bicycle, traffic light, stop sign, … |
| `assets/depth/midas-small.onnx` | Monocular depth (MiDaS small) — Tier 1 | 1×3×384×384 | relative depth map, fused with the homography for metric distance |
| `assets/depth/depth-anything-v2-small.onnx` | Monocular depth (Depth Anything V2 small) — Tier 2+ | dynamic (3×H×W, ViT/14 patch grid) | sharper relative depth; same homography calibration as MiDaS |
| `assets/seg/ddrnet23_slim.onnx` | Road segmentation (DDRNet23-Slim) — Tier 2+ | 1×3×1024×2048 RGB | 19 Cityscapes classes incl. road, sidewalk, building, pole, traffic light, car, person — ground-truth road mask for the 3D reconstruction |
| `autodrive_*.onnx` | Drivable-area / lane head (trainable — see docs/training.md) | 1×3×512×1024 | lane + drivable segmentation |
| `autosteer_*.onnx` | Steering / path head (trainable) | 1×3×512×1024 | path offset |

## Install

```bash
models/download_models.sh
```

or export YOLO11n yourself:

```bash
pip install ultralytics
python -c "from ultralytics import YOLO; YOLO('yolo11n.pt').export(format='onnx', imgsz=[512,1024], opset=17)"
cp yolo11n.onnx engine/modules/models/weights/
```

## Tiers

| Tier | Depth model | Segmentation | Where it's used |
|---|---|---|---|
| beginner | MiDaS-small | — | default; lightest, runs on any 4-core CPU |
| mid | Depth Anything V2 small | DDRNet23-Slim road mask | `run_pipeline.sh <video> <fps> mid` |
| top | Depth Anything V2 small | DDRNet23-Slim road mask | denser cloud (stride 1, 0.25 m voxel); GPU host for live rates |

The tier only changes **which model files are loaded and how densely the video
is sampled** — the model files themselves are identical on CPU and GPU.

## Runtime: CPU vs GPU

| | CPU | CPU + GPU |
|---|---|---|
| Runtime | ONNX Runtime CPU (in `third_party/onnxruntime-cpu/`) | ONNX Runtime CUDA / TensorRT build |
| CMake | `-DONNXRUNTIME_ROOT=third_party/onnxruntime-cpu` | `-DONNXRUNTIME_ROOT=<onnxruntime-cuda>` (see docs/gpu-setup.md) |
| Config | `engine.config.provider = cpu` (default) | `engine.config.provider = cuda` |

The model files are identical for both; only the runtime build changes.
Detailed setup: docs/cpu-setup.md and docs/gpu-setup.md.

## Why YOLO11n

- small (~10 M params) and CPU-realtime at 512×1024;
- 80 COCO classes so cars, trucks, buses, motorcycles, bicycles, pedestrians,
  traffic lights and signs are separated instead of merged into "vehicle";
- one-head export `[1, 84, N]` matches the engine post-processing contract
  (4 box + 80 class probabilities, no NMS inside the graph).
