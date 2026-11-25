# CPU setup

Adrient OpenVision is CPU-first: everything on this page runs on an ordinary
multi-core x86 machine with no GPU. Verified on Ubuntu Linux, 8 cores.

## 1. Prerequisites

```bash
sudo apt-get install -y build-essential cmake git ffmpeg libopencv-dev \
    libboost-all-dev xvfb curl
```

ONNX Runtime (CPU build) is bundled under `third_party/onnxruntime-cpu/` —
no separate install needed.

## 2. Build

```bash
bash scripts/run_pipeline.sh
```

First run configures CMake with `-DONNXRUNTIME_ROOT=$PWD/third_party/onnxruntime-cpu`
and builds. Or build manually:

```bash
cmake -S engine -B engine/build \
    -DCMAKE_BUILD_TYPE=Release -DENABLE_RERUN=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-cpu"
cmake --build engine/build -j"$(nproc)"
```

## 3. Download the models

```bash
models/download_models.sh        # fetches yolo11n.onnx + midas-small.onnx
```

## 4. Run

Offline (sample dataset):

```bash
bash scripts/run_pipeline.sh
python3 -m http.server 3000 --directory viewer   # open http://localhost:3000
```

Live camera / your own video: see the README "Run" section.

## 5. Performance

The pipeline runs realtime (10 fps) on 8 CPU cores: YOLO11n at 512×1024 is
the biggest cost, MiDaS-small the second. If you need more headroom:

- set `engine.provider = cpu` with fewer threads in
  `engine/config/openvision.conf`;
- run the viewer on a second machine — the perception box only writes JSON;
- drop input resolution (the detector degrades gracefully to 384×768).

Troubleshooting:

| Symptom | Fix |
|---|---|
| `Config file not found: yolo11n.onnx` | weights not downloaded — run `models/download_models.sh` |
| `ONNXRUNTIME_ROOT is not set` | pass `-DONNXRUNTIME_ROOT=$PWD/third_party/onnxruntime-cpu` |
| Slow (< 5 fps) | check `nproc` ≥ 4; close other jobs; see §5 |
