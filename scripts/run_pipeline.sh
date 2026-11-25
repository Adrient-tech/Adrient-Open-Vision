#!/usr/bin/env bash
# Adrient OpenVision — full pipeline: perception → world model → scene adapter.
# Usage: scripts/run_pipeline.sh [input_video_or_dataset_dir] [fps]
set -euo pipefail
cd "$(dirname "$0")/.."
FPS="${2:-10}"
BUILD=perception/VisionPilot/build

if [ ! -x "$BUILD/VisionPilot" ]; then
  echo "Building perception (first run)…"
  cmake -S perception/VisionPilot -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release -DENABLE_RERUN=ON \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-cpu" \
      -DBOOST_CMAKE_DIR="$PWD/third_party/boost-cmake"
  cmake --build "$BUILD" -j"$(nproc)"
fi

echo "1/3 Perception…"
LD_LIBRARY_PATH="$PWD/third_party/onnxruntime-cpu/lib:$LD_LIBRARY_PATH" \
  xvfb-run -a "$BUILD/VisionPilot"

echo "2/3 World model…"
(cd worldmodel && cargo run --release -- \
    "../$BUILD/perception.jsonl" "../$BUILD/world_state.jsonl" "$FPS")

echo "3/3 Scene adapter…"
(cd worldmodel && ./target/release/adapter \
    "../$BUILD/world_state.jsonl" ../viewer/scene.json "$FPS")

echo "Done. Serve the viewer:  python3 -m http.server 3000 --directory viewer"
