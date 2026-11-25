# Tiers: beginner / mid / top

One codebase, three fidelity levels. The pipeline takes the tier as its third
argument and the viewer mirrors it in the URL:

```bash
scripts/run_pipeline.sh <video.mp4> <fps> beginner   # default
scripts/run_pipeline.sh <video.mp4> <fps> mid
scripts/run_pipeline.sh <video.mp4> <fps> top

python3 -m http.server 3000 --directory viewer
# viewer:  index.html            → beginner render
#          index.html?tier=mid   → finer points
#          index.html?tier=top   → finest points
```

## What each tier changes

| | Beginner | Mid | Top |
|---|---|---|---|
| Depth model | `engine/assets/depth/midas-small.onnx` | `engine/assets/depth/depth-anything-v2-small.onnx` | same as mid |
| Road segmentation | none | `engine/assets/seg/ddrnet23_slim.onnx` via `reconstruct3d.py --road-mask` | same as mid |
| Frame stride / voxel | 3 / 0.45 m | 2 / 0.35 m | 1 / 0.25 m |
| Reconstruction step | yes (step 4/4) | yes, road-masked | yes, road-masked, densest |
| Viewer point size | 0.16 | 0.12 | 0.09 |

Tier never invents content: street furniture, crossings and buildings enter
the scene only when a model (detector or segmentation) actually sees them.

## System requirements

| | Minimum (beginner) | Recommended (mid) | Top tier |
|---|---|---|---|
| CPU | 4 cores | 8 cores | 12+ cores |
| RAM | 8 GB | 16 GB | 32 GB |
| GPU | none | 4 GB VRAM (GTX 1650 / RX 6400), optional | 12–24 GB VRAM (RTX 4070+ / 4090) |
| Live rate | offline only | ~5–10 fps GPU, ~2 fps CPU | 20–30 fps fused BEV + render |
| Storage | 5 GB | 10 GB | 50 GB (multi-cam + splats) |

## CPU vs CPU+GPU

The ONNX model files are identical; only the runtime build changes.

```bash
# CPU only (default)
cmake -S engine -B engine/build -DONNXRUNTIME_ROOT=$PWD/third_party/onnxruntime-cpu
# see docs/cpu-setup.md

# CPU + GPU (CUDA EP)
cmake -S engine -B engine/build -DONNXRUNTIME_ROOT=<onnxruntime-cuda> -DUSE_CUDA=ON
# see docs/gpu-setup.md
```

Python stages (`reconstruct3d.py`) follow `providers=["CPUExecutionProvider"]`
by default; on a GPU host install `onnxruntime-gpu` and set
`ORT_PROVIDER=CUDAExecutionProvider` (exported in `docs/gpu-setup.md`).

## Tier 3 — multi-camera BEV and live "GTA-style" view

Top tier needs more than one camera because a single front view has no real
depth. The path, in order (see docs/camera-setup.md for the hardware and
calibration):

1. **Camera ring** — dashcam + side/rear cams, each with its own `H.yaml`
   homography and intrinsics (`engine/Calibration/`).
2. **Per-camera depth + segmentation** — DAv2 + DDRNet per camera on the GPU.
3. **BEV fusion** — every camera's depth is lifted through its own homography
   into the same world frame (`scripts/reconstruct3d.py` already fuses one
   camera this way; multi-cam adds one session per camera and concatenates
   before the voxel downsample).
4. **Temporal fusion + occupancy grid** — voxel decay keeps stable surfaces and
   drops moving ghosts.
5. **Optional neural rendering** — gaussian splats or mesh over the corridor
   for the "real view" look.

Steps 3–5 are GPU-host work (no GPU on a laptop): run the engine on a rented
host per docs/gpu-setup.md (vastai/runpod/modal or SSH).
