# Adrient OpenVision — Open-Source Real-Time World Simulation: Scoping Note

Date: 2026-11-24. Goal: an open-source counterpart to what Tesla shipped in FSD v15 "Reality View" (Nov 21, 2026) — a generative visualization of the car's surroundings driven by its perception stack, plus their closed-loop neural world simulator for training. Sources: [Dealroom on Reality View's diffusion world model](https://dealroom.co/news/x-2124461034182197621-musk-explains-the-diffusion-world-model-behind-teslas-new-fsd-reality-vi/), [Embodied AI 101 episode on Tesla world models](https://embodied.transistor.fm/episodes/tesla-world-models-what-do-gaussian-splatting-and-meanflow-deliver/transcript), [AI Shift News on the CoRL Optimus world-model talk](https://aishiftnews.ca/news/tesla-shows-optimus-walking-inside-a-world-model-at-corl/), [Robotics Weekly on the shared FSD/Optimus world model](https://roboticsweekly.news/issues/2026-11-22).

## What Tesla actually shipped (verified via press coverage, Nov 2026)

- **Reality View** (FSD v15, Nov 21 2026): a ~1B-parameter conditional diffusion model forked off their Universal Vision Encoder, rendering 8-frame chunks at 36 fps on the car's screen; latency still high.
- **Neural world simulator** (Elluswamy, ICCV Oct 2025; CoRL Optimus talk Nov 2026): action-conditioned simulation — predicts future sensor state from current state + action, run closed-loop with the driving policy; Tesla claimed 8 camera streams at 24 fps for 6+ minutes. Tesla's numbers, not independently verified.
- **No public checkpoints, training code, or runnable research simulator exist** as of Nov 24 2026. So an open-source effort is genuinely填补空白的 — but must start far smaller.

## What Adrient has today (Phase 0–1 done)

Working CPU perception engine (Vision Pilot v1.2): 3 ONNX models (drive/steer/speed) at ~4 fps fp32 on 8 vCPU, fusion outputs (`Plan`, `LateralFusionEstimate`, `CIPOFusionEstimate`), HUD + heuristic Occupancy BEV, config/plumbing for Rerun and WebRTC streaming.

## Honest gap analysis

Reality View-style neural rendering needs: (a) a diffusion/flow world model (~1B params; Tesla's), (b) a GPU-class accelerator — this 8-core CPU box does 4 fps of *perception*, and neural frame generation is 10–100× heavier, (c) Tesla-scale driving video data. None of these are reachable by an open-source project in one step. But the *arc* is reachable in stages.

## Proposed staged roadmap (extends the PRD's phases)

1. **Replay + reconstruct (no GPU needed)** — Stream the perception outputs (Plan, fusion estimates, occupancy grid) into Rerun over WebRTC/HTTP so any viewer can watch the run live. Foundation for everything else; PRD Phase 5 alignment.
2. **Geometric world model (CPU-feasible)** — Extend the heuristic Occupancy BEV into a persistent 3D scene: track vehicles (CIPO + detections), lane graph, ego trajectory, integrated over time. Not photoreal, but it *is* a world model — a queryable state of the environment, like Tesla's bird-view lineage.
3. **Closed-loop physics simulation** — Couple the perception engine to an open-source simulator (CARLA or OpenCDA): simulator renders sensor input → Vision Pilot infers → commands feed back to the sim. This gives the thing that matters for an open project: reproducible scenario testing (cut-ins, CIPO events) — the open-source equivalent of Tesla's action-conditioned simulator, with physics rather than pixels.
4. **Neural rendering (GPU, later)** — Gaussian-splatting reconstruction from dashcam/replayed sequences (cf. the Apache-2.0 `dashsplat` project that reconstructs an orbitable scene from Tesla dashcam footage), then — much later — a small video-diffusion world model fine-tuned on driving data. Only worth starting once a GPU machine and step 3 exist.

## Recommendation

Commit to steps 1–3 as the open-source differentiator (reproducible closed-loop testing is what no Tesla disclosure gives the public), and treat photoreal generative simulation (step 4) as a later, GPU-dependent track. Next concrete task when wanted: step 1 — wire the Rerun/WebRTC output that v1.2 already builds behind flags, so the engine's world view is streamable.
