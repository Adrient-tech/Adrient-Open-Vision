# Adrient OpenVision

Open-source, CPU-first camera perception and 3D world simulation — the open
counterpart to Tesla's Reality View, built per the Adrient OpenVision PRD.

Per the decision of 2026-11-25, the engine stays **C++**; new code (world
model, adapters, tooling) is Rust. No full Rust rewrite.

## Pipeline

| Stage | What it does | Where | Status |
|---|---|---|---|
| 0 | Feasibility: hardware, pins, licences | `docs/phase0-feasibility-report.md` | done |
| 1 | CPU perception build + benchmark | `docs/phase1-cpu-build-results.md` | done |
| 1b | Perception streaming (Rerun + WebRTC HUD) | `docs/stage1-streaming.md` | done |
| 2 | Persistent world model (Rust) | `docs/stage2-world-model.md`, `engine/worldmodel/` | done |
| 2b | Scene adapter + branded 3D viewer | `viewer/`, `engine/worldmodel/src/bin/adapter.rs` | done |
| 3 | Closed-loop physics sim (CARLA, small rented GPU) | `docs/stage3-closed-loop.md` | next |
| 4 | Neural rendering (GPU-heavy, last) | — | not started |

## Quick tour

```bash
# 1. Perception (C++ engine, offline mode) — writes perception.jsonl
cd engine/upstream/VisionPilot/build
LD_LIBRARY_PATH=../../../onnxruntime-cpu/lib xvfb-run -a ./VisionPilot

# 2. World model (Rust) — persistent tracks, path fit, CIPO
cd ../../../worldmodel && cargo run --release -- \
    ../upstream/VisionPilot/build/perception.jsonl \
    ../upstream/VisionPilot/build/world_state.jsonl 10

# 3. Scene adapter (Rust) — world_state.jsonl → viewer scene.json
./target/release/adapter ../upstream/VisionPilot/build/world_state.jsonl \
    viewer/scene.json 10

# 4. Viewer — serve viewer/ and open it (3D scene, HUD, scrubber)
python3 -m http.server 3000 --directory viewer
```

## Brand

**Adrient OpenVision** — Adrient Private Limited. The viewer
(`viewer/index.html`) carries the product brand; the scene-message contract is
`adrient.openvision/scene@1`.
