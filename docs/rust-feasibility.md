# Can this be built entirely in Rust? — feasibility note

Date: 2026-11-25. Question from Adrient: "Can we build this entirely in Rust? Is it possible? How long will it take?" plus a budget note: GPU use is allowed, but sparingly.

## Short answer

Yes, it is possible — and part of it already is. The stage-2 world model
(`engine/worldmodel/`) is pure Rust today. Rewriting the rest of the pipeline
in Rust is realistic, but the perception network inference is the one part
where Rust buys no speed: it is ONNX Runtime doing the work either way.

## Measured today (this machine, 8 CPU cores)

```
cargo build --release  # engine/worldmodel
/usr/bin/time -v ./target/release/worldmodel \
    ../upstream/VisionPilot/build/perception.jsonl /tmp/world_state_timed.jsonl 10
  Elapsed (wall clock) time: 0:00.00   (197 frames)
  Maximum resident set size: 2592 kB
```

197 frames in under 10 ms — roughly 50 µs per frame, versus ~250 ms per frame
for the C++ perception stage. The Rust world model is effectively free; the
bottleneck is and stays the neural network inference, which is ONNX Runtime in
both languages.

## What is already Rust, and what would move

| Component | Today | Rust path | Effort |
|---|---|---|---|
| World model (tracking, CV filter, CIPO) | Rust | done | 0 |
| App shell, config, logging, I/O loop | C++ | straightforward | days |
| GStreamer / WebRTC HUD streaming | C++ | `gstreamer-rs` is mature; `webrtcbin` is reachable from it | 1–2 weeks |
| OpenCV image pre/post-processing | C++ | reimplement with `image`, `ndarray` or keep a thin C++ shim | 1–3 weeks (the pre/post ops are simple resize/normalize/decode; the risk is matching upstream numerics exactly) |
| ONNX Runtime inference | C++ API | `ort` crate calls the same runtime .so — same speed, safe wrapper | days |
| CARLA client (stage 3) | Python/C++ | `carla` crate exists but is unofficial; the Python client is the safe route initially | 1 week |
| Neural rendering (stage 4, GPU) | Python ecosystem (gsplat etc.) | not worth rewriting; the GPU/pytorch ecosystem is where it lives | out of scope for Rust |

## Realistic timeline

- **Weekend probe (recommended first step):** wrap inference with the `ort`
  crate and feed it the same frames, compare per-frame latency with the C++
  build. Proves the speed-parity assumption for ~1 day of work.
- **Full Rust pipeline replacing the C++ app: 6–10 weeks** of focused work for
  one engineer — inference + pre/post-processing + streaming + config, with
  parity checks against the C++ build at each step. Call it **2–3 months** to
  reach "does everything Vision Pilot v1.2 does, in Rust".
- The stage-2 world model and stage-3 scenario harness are Rust-adjacent
  already; those add no Rust risk.

## Is it worth it?

Honest trade-off: Rust gives memory safety, easy cross-compilation and a much
easier time embedding the world model (already done). It does not make the
perception faster — inference dominates. The pragmatic plan is the strangler
pattern we are already following: new stages (world model, sim harness) in
Rust from day one, and port the C++ perception shell only if/when the
prototype needs to ship as a product. Building stage 2 and 3 first costs
nothing extra in Rust; a full rewrite before the prototype is proven would
delay the roadmap by ~2 months for zero accuracy gain.

## GPU budget note

Per the "not too much GPU" constraint: stages 1–2 (streaming, world model) run
entirely on CPU as verified. Stage 3 (CARLA) needs a GPU only for rendering —
a small rented instance (a single RTX-class card on vastai/runpod, a few
dollars per run) suffices for headless low-res scenario testing. Stage 4
(neural rendering) is the only genuinely GPU-hungry stage and is deliberately
last; it can be skipped until the first three stages have proven the loop.
