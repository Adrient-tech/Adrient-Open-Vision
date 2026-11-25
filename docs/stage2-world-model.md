# Stage 2 — Persistent World Model (Rust)

Date: 2026-11-25. Builds on stage 1 (`docs/stage1-streaming.md`); roadmap in `docs/simulation-roadmap.md`.

## What was built and verified

The engine's per-frame world view is now fused over time into a **persistent world model**, implemented as a standalone **Rust** crate (`worldmodel/`) — a working proof that the non-CV parts of this stack build and run in Rust.

Two pieces:

1. **Perception feed (C++, ~50 lines).** `VisionPilot` now writes one JSONL line per frame when `world_log_on = true` in `config/vision_pilot.conf`: ego speed, fused CIPO, path polynomial, and every AutoSpeed detection projected to world coordinates through the calibrated homography `H_resized` (bottom-centre pixel, same convention as CIPO selection). Fields: `frame_id, ego_v, wall_ms, cipo{...}, path{a,b,c,cte,yaw,kappa}, dets[{cls,score,x,y}]`. No upstream algorithm code changed.

2. **World model (Rust, `worldmodel/src/main.rs`).** Consumes the JSONL and maintains ground-fixed world state:
   - ego odometry: `x += v·dt` per frame;
   - object tracks: greedy nearest-neighbour association with an adaptive covariance gate, per-track constant-velocity filter (position + world-frame velocity), track lifecycle (tentative → confirmed after 3 hits, deleted after 10 misses);
   - CIPO over tracks: nearest confirmed track on the path ahead (`|y| < 1.8 m`), replacing the per-frame-only estimate with a persistent identity;
   - emits `world_state.jsonl` (ego pose, path, tracks with id/pos/vel/state, tracked CIPO) and a summary.

## Verified numbers (sample run `test_open_lane_2`, 197 frames)

Command: `cd perception/VisionPilot/build && LD_LIBRARY_PATH=../../../onnxruntime-cpu/lib xvfb-run -a ./VisionPilot` (exit 0), then `cd worldmodel && cargo run --release -- ../upstream/VisionPilot/build/perception.jsonl ../upstream/VisionPilot/build/world_state.jsonl 10` (exit 0).

- 627 detections consumed (avg 3.18/frame), engine CIPO valid on 197/197 frames.
- 186 track identities created, **54 tracks confirmed** (≥3 hits), 2 alive at end of run.
- Confirmed tracks present on 192/197 frames (97%).
- Tracked-CIPO (persistent identity) available on 135/197 frames (69%) — lower than the engine's 100% because it requires a confirmed track, not a raw detection.
- Ego distance integrated: 347.7 m.
- Rust model runtime: **0.041 s** for the whole sequence (`time` on the release build) — negligible next to the ~4 fps perception engine.

## Reproduce

```bash
# 1. perception feed (engine run, ~50 s offline)
cd perception/VisionPilot/build
LD_LIBRARY_PATH=../../../onnxruntime-cpu/lib xvfb-run -a ./VisionPilot
# → perception.jsonl

# 2. world model
cd ../../../worldmodel
cargo run --release -- ../upstream/VisionPilot/build/perception.jsonl \
       ../upstream/VisionPilot/build/world_state.jsonl 10   # 10 = assumed capture fps
```

## Assumptions / caveats

- Capture fps assumed **10 Hz** (the 3rd CLI argument); only `dt` depends on it. The sample videos do not state their fps, so velocities scale with this assumption.
- Detections are sparse (avg 3.2/frame — the detector targets CIPO-relevant objects), so track fragmentation is expected; 54 confirmed identities over 348 m is plausible for lead-vehicle changes (the engine log shows several CIPO target changes).
- The world model assumes the ground plane and lateral sign convention of `H.yaml`; nothing here re-calibrates.

## GPU status

Stage 2 is pure CPU work — no GPU was needed or used. This machine has no GPU; when stage 3/4 needs one (CARLA rendering, neural rendering), a rented host (Vast/RunPod/Lambda via their CLIs, or SSH) is the path; credentials would come from the Vault.
