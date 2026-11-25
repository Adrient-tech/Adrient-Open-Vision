# CPU + GPU setup

The same models and pipeline run GPU-accelerated: only the ONNX Runtime build
and one config line change. The model files are identical for CPU and GPU.

## 1. Get a GPU-enabled ONNX Runtime

Download the CUDA build (match CUDA/cuDNN to your driver; CUDA 12 packages
need cuDNN 9):

```bash
mkdir -p third_party/onnxruntime-gpu && cd third_party/onnxruntime-gpu
curl -fLO https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-gpu-1.20.1.tgz
tar xzf onnxruntime-linux-x64-gpu-1.20.1.tgz --strip-components=1
```

Verify CUDA/cuDNN are visible:

```bash
LD_LIBRARY_PATH=$PWD/lib ./lib/onnxruntime_providers_shared.so 2>&1 | head -2
nvidia-smi
```

## 2. Build against the GPU runtime

```bash
cmake -S engine -B engine/build-gpu \
    -DCMAKE_BUILD_TYPE=Release -DENABLE_RERUN=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-gpu"
cmake --build engine/build-gpu -j"$(nproc)"
```

Keep the CPU build in `engine/build/` — you can switch between them without
rebuilding.

## 3. Run with the CUDA provider

Set the provider in the config (or leave the default `cpu` to keep the CPU
runtime in the same binary):

```bash
echo "engine.provider = cuda" >> engine/build-gpu/config/openvision.conf
LD_LIBRARY_PATH="$PWD/third_party/onnxruntime-gpu/lib" ./engine/build-gpu/OpenVision
```

The engine registers CUDAExecutionProvider first and falls back to CPU per
session, so a graph that cannot run on GPU still runs.

## 4. Which models benefit

| Model | GPU speed-up | Note |
|---|---|---|
| YOLO11n (detector) | large | becomes near-instant; raises usable fps or resolution |
| MiDaS-small (depth) | large | the second-biggest CPU cost |
| autodrive / autosteer heads | moderate | small models, already fast on CPU |

On a single 8 GB GPU the whole pipeline uses < 3 GB VRAM at 512×1024.

## 5. Remote GPU machines (no GPU locally)

This project develops CPU-first; GPU stages run on a rented host via the
vastai/runpod/modal CLIs or SSH:

```bash
vastai search "gpu rtx 3090 disk>=50"      # pick an instance
rsync -a --exclude engine/build ./ user@host:/opt/openvision/
ssh user@host "cd /opt/openvision && bash scripts/run_pipeline.sh"
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `[E:onnxruntime:DefaultProviderRegistry]` CUDA not available | `LD_LIBRARY_PATH` missing the gpu `lib/` dir, or CUDA/cuDNN version mismatch |
| Falls back to CPU silently | check `nvidia-smi` while running; enable ORT logging (`ORT_LOG_LEVEL=VERBOSE`) |
| `out of memory` | lower batch (always 1 here) or resolution; check other GPU processes |
