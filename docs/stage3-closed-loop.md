# Stage 3 — Closed-Loop Simulation (CARLA)

Status: **next**, wired design only. This machine has no GPU; CARLA needs a
small rented GPU host (vastai/runpod/modal, credentials from Vault; a few
dollars per run).

## Design

Stage 3 closes the loop: instead of replaying a recorded feed, the sim feeds
synthetic camera frames back into the C++ perception engine and checks the
world model's decisions against the simulator's ground truth.

```
CARLA (GPU host) ──camera sensor──▶ VisionPilot (perception, can stay CPU)
      ▲                                    │ perception.jsonl
      │                                    ▼
      └── controls ◀──────────── worldmodel (Rust) ──▶ scene.json ──▶ viewer
```

## Plan

1. Rent one small GPU host (e.g. RTX 3060-class), install CARLA 0.10 + the
   Python API there.
2. Run VisionPilot + worldmodel on the same host (or stream `perception.jsonl`
   back to this machine over SSH) — the CPU stages already run at ~4 fps.
3. Bridge: a Python client spawns ego + traffic, grabs camera frames, drives
   VisionPilot's input; the world model's CIPO/path output is compared against
   CARLA ground truth (lead distance, lane offset) per frame.
4. Metric: world-model state error vs ground truth (m and m/s), reported as a
   per-run summary JSON alongside `scene.json`.

Stages 1–2b remain fully reproducible on this CPU machine; nothing above
depends on stage 3 being run locally.
