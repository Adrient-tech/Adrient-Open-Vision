// OpenVision adapter — normalises world-model output for the 3D viewer.
//
// Input : world_state.jsonl from the stage-2 world model (per-frame persistent
//         tracks + ego pose + fitted path polynomial).
// Output: scene.json — the Adrient-branded, normalised scene message consumed
//         by the OpenVision 3D viewer (React + Three.js).
//
// Contract (schema version 2): adds the model class id per track so the viewer
// can render class-aware shapes (vehicle vs person vs two-wheeler), plus a
// per-class canonical footprint (width, length, height in metres) for sizing.
//   { schema, fps, frames: [{ t, frame_id, ego { x, v },
//      path: [[x, y], ...],  // sampled plan polyline, ego frame, forward x
//      vehicles: [{ id, cls, kind, w, l, h, x, y, vx, vy, state }],
//      cipo: { valid, track_id, d, v_rel, cut_in } }] }
//
// Usage: adapter <world_state.jsonl> <scene.json> [fps]

use serde::Deserialize;
use std::env;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Write};

#[derive(Deserialize)]
struct WorldFrame {
    frame_id: u64,
    ego: Ego,
    #[serde(default)]
    path: PathIn,
    #[serde(default)]
    tracks: Vec<Track>,
    #[serde(default)]
    cipo: Cipo,
}

#[derive(Deserialize)]
struct Ego {
    x: f64,
    v: f64,
}

#[derive(Deserialize, Default)]
struct PathIn {
    a: f64,
    b: f64,
    c: f64,
}

#[derive(Deserialize)]
struct Track {
    id: u32,
    x: f64,
    y: f64,
    vx: f64,
    vy: f64,
    state: String,
    #[serde(default)]
    cls: i32,
}

#[derive(Deserialize, Default)]
struct Cipo {
    #[serde(default)]
    valid: bool,
    track_id: Option<u32>,
    d: Option<f64>,
    #[serde(default)]
    v_rel: f64,
    #[serde(default)]
    cut_in: bool,
}

const PATH_X_MAX: f64 = 60.0;
const PATH_STEPS: usize = 24;

// Model class id → viewer kind + canonical footprint (w, l, h) in metres.
// Assumed mapping for the 4-class AutoSpeed-style head (C = 4 box + 4 classes):
// 0 = car, 1 = person, 2 = bus/truck, 3 = car (default). Class 1 in the current
// dataset tracks the steady lead object in the ego lane, so anything unmapped
// renders as a vehicle.
fn class_kind(cls: i32) -> (&'static str, f64, f64, f64) {
    match cls {
        1 => ("person", 0.6, 0.6, 1.7),
        2 => ("truck", 2.5, 8.0, 3.5),
        4 => ("bicycle", 0.8, 1.8, 1.7),
        _ => ("vehicle", 2.0, 4.6, 1.6),
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: adapter <world_state.jsonl> <scene.json> [fps]");
        std::process::exit(2);
    }
    let fps: f64 = args.get(3).map(|s| s.parse().unwrap()).unwrap_or(10.0);

    // Resolve a bare output filename against the repository root so the
    // adapter works from any cwd (e.g. `viewer/scene.json` from worldmodel/).
    let out_path = std::path::Path::new(&args[2]);
    let cwd = std::env::current_dir()?;
    let out_path = if out_path.parent().map_or(false, |p| p.exists()) {
        cwd.join(out_path)
    } else {
        let repo_root = cwd
            .ancestors()
            .find(|p| p.join("engine").exists() && p.join("viewer").exists())
            .map(|p| p.to_path_buf())
            .ok_or("repository root not found: pass an output path with an existing parent directory")?;
        repo_root.join(out_path)
    };

    let input = File::open(&args[1]).expect("open world_state.jsonl");
    let out = File::create(&out_path).expect("create scene.json");
    let mut out = BufWriter::new(out);

    out.write_all(b"{\"schema\":\"adrient.openvision/scene@2\",\"fps\":")?;
    serde_json::to_writer(&mut out, &fps)?;
    out.write_all(b",\"frames\":[")?;

    let mut first = true;
    for line in BufReader::new(input).lines() {
        let f: WorldFrame = serde_json::from_str(&line.unwrap()).expect("parse world frame");
        let t = f.frame_id as f64 / fps;
        if !first {
            out.write_all(b",")?;
        }
        first = false;

        // Plan polynomial sampled to a polyline the viewer can draw directly.
        let mut path = Vec::with_capacity(PATH_STEPS + 1);
        for i in 0..=PATH_STEPS {
            let x = PATH_X_MAX * i as f64 / PATH_STEPS as f64;
            let y = f.path.a * x * x + f.path.b * x + f.path.c;
            path.push([round2(x), round2(y)]);
        }

        serde_json::to_writer(
            &mut out,
            &serde_json::json!({
                "t": round2(t),
                "frame_id": f.frame_id,
                "ego": { "x": round2(f.ego.x), "v": round2(f.ego.v) },
                "path": path,
                "vehicles": f.tracks.iter().map(|tr| {
                    let (kind, w, l, h) = class_kind(tr.cls);
                    serde_json::json!({
                        "id": tr.id,
                        "cls": tr.cls,
                        "kind": kind,
                        "w": w,
                        "l": l,
                        "h": h,
                        "x": round2(tr.x),
                        "y": round2(tr.y),
                        "vx": round2(tr.vx),
                        "vy": round2(tr.vy),
                        "state": tr.state,
                    })
                }).collect::<Vec<_>>(),
                "cipo": serde_json::json!({
                    "valid": f.cipo.valid,
                    "track_id": f.cipo.track_id,
                    "d": f.cipo.d.map(round2),
                    "v_rel": round2(f.cipo.v_rel),
                    "cut_in": f.cipo.cut_in,
                }),
            }),
        )?;
    }

    out.write_all(b"]}")?;
    out.flush()?;
    Ok(())
}

fn round2(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}
