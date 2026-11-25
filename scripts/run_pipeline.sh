#!/usr/bin/env bash
# Adrient OpenVision — full pipeline: perception → world model → scene adapter.
# Usage: scripts/run_pipeline.sh [input_video_or_dataset_dir] [fps]
set -euo pipefail
cd "$(dirname "$0")/.."
FPS="${2:-10}"
BUILD=engine/build

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

echo "1/3 Perception…"
cd "$BUILD"
cp -n ../config/openvision.conf . 2>/dev/null || true
# Offline run: dataset input, CPU provider, no realtime clock.
# The app reads input paths from openvision_test.conf (see modules/config/src);
# find_config() prefers <cwd>/config/, so patch the copies inside build/config.
cat > config/openvision_test.conf <<CFG
source.video_realtime      = false
source.video_loop          = false
source.input_video         = $OLDPWD/data/sample_dataset/test_open_lane_10/input.mp4
source.input_vehicle_speed = $OLDPWD/data/sample_dataset/test_open_lane_10/frame_speed.txt
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

echo "2/3 World model…"
(cd worldmodel && cargo run --release --bin worldmodel -- \
    "../$BUILD/perception.jsonl" "../$BUILD/world_state.jsonl" "$FPS")

echo "3/3 Scene adapter…"
(cd worldmodel && cargo build --release --bin adapter && ./target/release/adapter \
    "../$BUILD/world_state.jsonl" ../viewer/scene.json "$FPS")

echo "Done. Serve the viewer:  python3 -m http.server 3000 --directory viewer"
