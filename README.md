# Adrient OpenVision

Open-source, CPU-first camera perception and 3D world simulation — the open
counterpart to Tesla's Reality View, built per the Adrient OpenVision PRD.

One project, four components (all vendored here; no nested git repos):

| Folder | What it is | Language |
|---|---|---|
| `perception/` | VisionPilot v1.2 camera perception engine (lanes, leads, occupancy) | C++ |
| `worldmodel/` | Persistent world model (tracking, path fit, CIPO) + scene adapter | Rust |
| `viewer/` | Branded 3D scene viewer (`adrient.openvision/scene@1`) | JS/Three.js |
| `third_party/` | ONNX Runtime (CPU build), Boost CMake stub | C/C++ |
| `data/sample_dataset/` | Sample driving sequences for offline runs | — |

Per the decision of 2026-11-25, the engine stays **C++**; new code (world
model, adapters, tooling) is Rust. Depth Anything is not integrated: the
perception engine already covers the depth-dependent stages (occupancy grid,
lead distance) for v1 — revisit only if the viewer needs dense depth.

## Run everything

```bash
scripts/run_pipeline.sh          # build if needed, then perception → world model → scene.json
python3 -m http.server 3000 --directory viewer   # then open http://localhost:3000
```

## Stage status

| Stage | What | Doc | Status |
|---|---|---|---|
| 0–1 | Feasibility + CPU build/benchmark | `docs/phase0-*`, `docs/phase1-*` | done |
| 1b | Perception streaming (Rerun + WebRTC HUD) | `docs/stage1-streaming.md` | done |
| 2 | World model + adapter + viewer | `docs/stage2-world-model.md` | done |
| 3 | Closed-loop sim (CARLA, rented GPU) | `docs/stage3-closed-loop.md` | next |
| 4 | Neural rendering (GPU-heavy) | — | not started |

## Licences

VisionPilot is Apache-2.0; its `LICENSE` and `DISCLAIMER.md` are kept in
`perception/` (attribution retained as required). ONNX Runtime is MIT. The
project's own code (worldmodel, viewer, scripts) follows the same Apache-2.0
grant as the upstream engine.
