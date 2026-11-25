# Stage 2 — World Model

- Stage 2 is done: `worldmodel/` (Rust crate) consumes `perception.jsonl` written by the VisionPilot app (`world_log_on = true` in build/config/vision_pilot.conf) and emits `world_state.jsonl` with persistent tracks (CV filter, confirm at 3 hits, delete at 10 misses).
- Run engine from `perception/VisionPilot/build`: `LD_LIBRARY_PATH=../../../onnxruntime-cpu/lib xvfb-run -a ./VisionPilot` (offline mode, `source.video_realtime = false`).
- World model: `cd worldmodel && cargo run --release -- ../upstream/VisionPilot/build/perception.jsonl ../upstream/VisionPilot/build/world_state.jsonl 10` (10 = assumed capture fps, only dt depends on it).
- Machine has no GPU; GPU stages (CARLA, neural rendering) need a rented host via vastai/runpod/modal CLIs or SSH, credentials from Vault.
