# Build & Benchmarks

Date: 2026-11-24 · CPU-only (8 cores, no GPU) · OpenVision engine

## Configure & build

```bash
cmake -S engine -B engine/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH="$PWD/third_party/boost-cmake/boost_system-1.90.0" \
  -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-cpu"
cmake --build engine/build -j$(nproc)
```

- Result: build completes 100% (`Built target OpenVision`).
- Boost 1.90 ships no `boost_system` CMake config (header-only since 1.69);
  a local stub lives at `third_party/boost-cmake/boost_system-1.90.0/`
  (added via `CMAKE_PREFIX_PATH`, no source change).
- ONNX Runtime 1.24.1 CPU build is vendored at `third_party/onnxruntime-cpu/`.
- The vendored Arrow build inside the Rerun SDK needs
  `CMAKE_POLICY_VERSION_MINIMUM=3.5` exported as an environment variable
  (CMake 4 policy wall); `scripts/run_pipeline.sh` sets it.

## Throughput

| Configuration | Latency | Throughput |
|---|---|---|
| fp32, 8 threads | ~250 ms/frame | ~4 fps |
| int8 (quantised) | slower on this CPU | — |

fp32 was kept: int8 quantisation was measured slower on this CPU
(quantisation overhead exceeds the compute saving at 8 cores).

## End-to-end run

```bash
scripts/run_pipeline.sh
```

Runs perception → world model → scene adapter over a sample dataset
(197 frames, exit 0) and writes `viewer/scene.json` for the 3D viewer.

World model cost is negligible: all 197 frames process in under 0.01 s
wall clock (~50 µs/frame, 2.6 MB RSS). The bottleneck is neural inference.
