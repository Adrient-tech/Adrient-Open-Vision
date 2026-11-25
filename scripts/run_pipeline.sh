#!/usr/bin/env bash
# Adrient OpenVision — full pipeline:
#   1/4 perception (YOLO11n detector + MiDaS/DAv2 depth)
#   2/4 world model (persistent 3D tracks)
#   3/4 scene adapter (viewer/scene.json)
#   4/4 3D reconstruction (fused coloured point cloud → viewer/assets/)
#
# Usage: scripts/run_pipeline.sh [input_video_or_dataset_dir] [fps] [tier]
#   tier: beginner | mid | top   (default: beginner)
#     beginner — CPU-only, MiDaS-small depth, plain box render.
#     mid      — CPU-capable, Depth Anything V2 small + DDRNet road segmentation
#                (sharper geometry, ground points filtered by real road mask).
#     top      — same models as mid, denser cloud (smaller voxel, smaller stride);
#                multi-camera BEV fusion and neural rendering need a GPU host —
#                see docs/gpu-setup.md and docs/camera-setup.md.
set -euo pipefail
cd "$(dirname "$0")/.."
FPS="${2:-10}"
TIER="${3:-${TIER:-beginner}}"
BUILD=engine/build
VIDEO="${1:-data/sample_dataset/test_open_lane_10/input.mp4}"

case "$TIER" in
  beginner) DEPTH=engine/assets/depth/midas-small.onnx;  STRIDE=3; VOXEL=0.45; SEG=0 ;;
  mid)      DEPTH=engine/assets/depth/depth-anything-v2-small.onnx; STRIDE=2; VOXEL=0.35; SEG=1 ;;
  top)      DEPTH=engine/assets/depth/depth-anything-v2-small.onnx; STRIDE=1; VOXEL=0.25; SEG=1 ;;
  *) echo "Unknown tier '$TIER' (use beginner|mid|top)"; exit 2 ;;
esac

if [ ! -x "$BUILD/OpenVision" ]; then
  echo "Building perception (first run)…"
  export CMAKE_POLICY_VERSION_MINIMUM=3.5   # Arrow/mimalloc sub-builds need it too
  cmake -S engine -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release -DENABLE_RERUN=ON \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_PREFIX_PATH="$PWD/third_party/boost-cmake/boost_system-1.90.0" \
      -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-cpu"
  cmake --build "$BUILD" -j"$(nproc)"
fi

echo "1/4 Perception…"
cd "$BUILD"
cp -n ../config/openvision.conf . 2>/dev/null || true
# Offline run: dataset input, CPU provider, no realtime clock.
# The app reads input paths from openvision_test.conf (see modules/config/src);
# find_config() prefers <cwd>/config/, so patch the copies inside build/config.
cat > config/openvision_test.conf <<CFG
source.video_realtime      = false
source.video_loop          = false
source.input_video         = $OLDPWD/$VIDEO
source.input_vehicle_speed = ${OLDPWD}/$(dirname "$VIDEO")/frame_speed.txt
source.dataset             = open_lane
CFG
grep -q "OpenVision pipeline overrides" config/openvision.conf || cat >> config/openvision.conf <<CFG

# ─── OpenVision pipeline overrides (added by scripts/run_pipeline.sh) ───
engine.provider            = cpu
world_log_on               = true
CFG
LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:$OLDPWD/third_party/onnxruntime-cpu/lib" \
  xvfb-run -a ./OpenVision
cd "$OLDPWD"

echo "2/4 World model…"
(cd worldmodel && cargo run --release --bin worldmodel -- \
    "../$BUILD/perception.jsonl" "../$BUILD/world_state.jsonl" "$FPS")

echo "3/4 Scene adapter…"
(cd worldmodel && cargo build --release --bin adapter && ./target/release/adapter \
    "../$BUILD/world_state.jsonl" ../viewer/scene.json "$FPS")

echo "4/4 3D reconstruction (tier: $TIER)…"
RECON_ARGS=("$VIDEO" "$BUILD/world_state.jsonl" engine/config/H.yaml "$DEPTH" viewer/assets "$STRIDE" "$VOXEL")
if [ "$SEG" = 1 ]; then
  [ -f engine/assets/seg/ddrnet23_slim.onnx ] || \
    { echo "Tier mid/top needs engine/assets/seg/ddrnet23_slim.onnx — run models/download_models.sh"; exit 1; }
  RECON_ARGS+=("--road-mask" "engine/assets/seg/ddrnet23_slim.onnx")
fi
[ -f "$DEPTH" ] || { echo "Depth model missing: $DEPTH — run models/download_models.sh"; exit 1; }
python3 scripts/reconstruct3d.py "${RECON_ARGS[@]}"

echo "Done. Serve the viewer:  python3 -m http.server 3000 --directory viewer"
