#!/usr/bin/env bash
# Download every ONNX model Adrient OpenVision uses, into engine/modules/models/weights/.
# Usage:  models/download_models.sh [--gpu]
# The --gpu flag also fetches the CUDA-enabled ONNX Runtime notes (see docs/gpu-setup.md);
# the model files themselves are identical for CPU and GPU.
set -euo pipefail
cd "$(dirname "$0")/../engine"
ASSETS_DEPTH=assets/depth
ASSETS_SEG=assets/seg
WEIGHTS=modules/models/weights
mkdir -p "$ASSETS_DEPTH" "$ASSETS_SEG" "$WEIGHTS"

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

# 2) MiDaS small monocular depth (Tier 1 / beginner).
fetch "https://github.com/intel-isl/MiDaS/releases/download/v2_1_model/small.onnx" "$ASSETS_DEPTH/midas-small.onnx" || true

# 3) Depth Anything V2 small (Tier 2+ / mid & top) — sharper monocular depth,
#    dynamic input size. ~99 MB.
fetch "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/main/onnx/model.onnx" \
      "$ASSETS_DEPTH/depth-anything-v2-small.onnx" || {
  echo "depth-anything-v2-small.onnx download failed — convert from the checkpoint:"
  echo "  pip install depth-anything-v2 onnx transformers"
  echo "  (see docs/training.md → Export your own models)"
  exit 1
}

# 4) DDRNet23-Slim road segmentation (Tier 2+ / mid & top) — 19 Cityscapes
#    classes incl. road/sidewalk; used by reconstruct3d --road-mask and the
#    engine's road-pixel mask. ~23 MB.
fetch "https://huggingface.co/qualcomm/DDRNet23-Slim/resolve/main/README.md" /dev/null || true
fetch "https://qaihub-public-assets.s3.us-west-2.amazonaws.com/qai-hub-models/models/ddrnet23_slim/releases/v0.67.0/ddrnet23_slim-onnx-float.zip" \
      /tmp/ddrnet23_slim.zip && {
  unzip -oq /tmp/ddrnet23_slim.zip -d /tmp/ddrnet23_slim
  python3 - "$ASSETS_SEG/ddrnet23_slim.onnx" <<'PY'
import onnx, sys, os
from pathlib import Path
src = next(Path('/tmp/ddrnet23_slim').rglob('ddrnet23_slim.onnx'))
m = onnx.load(str(src), load_external_data=True)
onnx.save(m, sys.argv[1])
print('saved', sys.argv[1], os.path.getsize(sys.argv[1]))
PY
}

# 5) Lane/drive-area heads shipped with the repo (autodrive, autosteer).
#    If weights/ is missing them, copy from a release or your own training run.

echo
echo "Models under engine/:"
ls -la "$ASSETS_DEPTH" "$ASSETS_SEG" "$WEIGHTS"
