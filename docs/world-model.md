# World Model

The world model (`worldmodel/`, Rust) turns the perception engine's per-frame
output into a persistent, stable world state.

## Input: perception feed

The engine writes one JSONL line per frame when `world_log_on = true` in
`config/openvision.conf`: ego speed, fused closest-in-path object (CIPO), path
polynomial, and every detection projected to world coordinates through the
calibrated homography `H_resized` (bottom-centre pixel, same convention as
CIPO selection).

Fields: `frame_id, ego_v, wall_ms, cipo{...}, path{a,b,c,cte,yaw,kappa},
dets[{cls,score,x,y}]`.

## Tracking

- Constant-velocity filter per object, gated by distance and class.
- New detections become **tentative** tracks; confirmed after 3 consecutive
  hits; deleted after 10 consecutive misses.
- Track states: `tentative`, `confirmed`, `trail`.

## Output: world state

Per frame: ego state, sampled path polyline (quadratic fit a·x² + b·x + c),
all tracks with world positions and velocities, and the fused CIPO record
(distance, relative speed, cut-in flag).

## Run

```bash
cd worldmodel
cargo run --release -- <perception.jsonl> <world_state.jsonl> <fps>
```

## Scene adapter

`worldmodel/src/bin/adapter.rs` converts `world_state.jsonl` into
`viewer/scene.json` (schema `adrient.openvision/scene@1`) — sampled path
polyline, per-track records and the CIPO record, rounded to 2 decimals.

```bash
worldmodel/target/release/adapter <world_state.jsonl> viewer/scene.json <fps>
```

The whole chain is wired in `scripts/run_pipeline.sh`.
