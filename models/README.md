# Models

Every neural network Adrient OpenVision runs, all ONNX so the same files work
on CPU and (with the CUDA build of ONNX Runtime) GPU.

| File (engine/modules/models/weights/) | Task | Input | Classes / output |
|---|---|---|---|
| `yolo11n.onnx` | Object detection (YOLO11n) | 1×3×512×1024 | 80 COCO classes: car, person, truck, bus, motorcycle, bicycle, traffic light, stop sign, … |
| `midas-small.onnx` | Monocular depth (MiDaS small) | 1×3×384×384 | relative depth map, fused with the homography for metric distance |
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
