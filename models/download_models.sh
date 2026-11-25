#!/usr/bin/env bash
# Download every ONNX model Adrient OpenVision uses, into engine/modules/models/weights/.
# Usage:  models/download_models.sh [--gpu]
# The --gpu flag also fetches the CUDA-enabled ONNX Runtime notes (see docs/gpu-setup.md);
# the model files themselves are identical for CPU and GPU.
set -euo pipefail
cd "$(dirname "$0")/../engine/modules/models/weights"

fetch () { # fetch <url> <file>
  [ -s "$2" ] && { echo "have $2"; return; }
  echo "downloading $2 …"
  curl -fL --retry 3 -o "$2" "$1"
}

# 1) YOLO11n object detector — 80 COCO classes (car, person, truck, bus,
#    motorcycle, bicycle, traffic light, stop sign, …), exported at the
#    pipeline input size 512x1024.
fetch "https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.onnx" yolo11n.onnx || {
  echo "yolo11n.onnx download failed — export it locally instead:"
  echo "  pip install ultralytics && python -c \"from ultralytics import YOLO; YOLO('yolo11n.pt').export(format='onnx', imgsz=[512,1024], opset=17)\""
  echo "  (then move yolo11n.onnx here)"
  exit 1
}

# 2) MiDaS small monocular depth.
fetch "https://github.com/intel-isl/MiDaS/releases/download/v2_1_model/small.onnx" midas-small.onnx || true

# 3) Lane/drive-area heads shipped with the repo (autodrive, autosteer).
#    If weights/ is missing them, copy from a release or your own training run.

echo
echo "Models in $(pwd):"
ls -la
