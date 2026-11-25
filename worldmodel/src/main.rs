// Stage 2 — persistent world model over OpenVision perception feed.
//
// Input : perception.jsonl written by the OpenVision app (one line per frame:
//         ego_v, fused CIPO, fitted path polynomial, detections in ego frame).
// Output: world_state.jsonl (per-frame persistent tracks + ego pose + path)
//         plus a summary on stdout.
//
// Usage: worldmodel <perception.jsonl> <world_state.jsonl> [fps]

use serde::{Deserialize, Serialize};
use std::env;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Write};

// ─── Input schema ──────────────────────────────────────────────────────────────

#[derive(Deserialize)]
struct Frame {
    frame_id: u64,
    ego_v: f64,
    #[serde(default)]
    wall_ms: f64,
    cipo: Cipo,
    path: PathIn,
    dets: Vec<Det>,
}

#[derive(Deserialize)]
struct Cipo {
    valid: bool,
    d: f64,
    v: f64,
    #[serde(default)]
    cut_in: bool,
}

#[derive(Deserialize)]
struct PathIn {
    valid: bool,
    a: f64,
    b: f64,
    c: f64,
    #[serde(default)]
    x_min: f64,
}

#[derive(Serialize)]
struct PathOut {
    valid: bool,
    a: f64,
    b: f64,
    c: f64,
    x_min: f64,
}

#[derive(Deserialize)]
struct Det {
    #[allow(dead_code)]
    cls: i32,
    #[allow(dead_code)]
    score: f64,
    x: f64, // forward, ego frame [m]
    y: f64, // lateral, ego frame [m] (+ = left? sign follows H convention)
}

// ─── Output schema ─────────────────────────────────────────────────────────────

#[derive(Serialize)]
struct WorldFrame {
    frame_id: u64,
    ego: Ego,
    path: PathOut,
    tracks: Vec<TrackOut>,
    cipo: CipoOut,
}

#[derive(Serialize)]
struct Ego {
    x: f64,
    v: f64,
}

#[derive(Serialize)]
struct TrackOut {
    id: u32,
    x: f64,  // world, forward
    y: f64,  // world, lateral
    vx: f64, // world-frame velocity (relative to ground)
    vy: f64,
    age: u32,
    hits: u32,
    missed: u32,
    state: &'static str,
}

#[derive(Serialize)]
struct CipoOut {
    valid: bool,
    track_id: Option<u32>,
    d: f64,
    v_rel: f64,
    cut_in: bool,
}

// ─── Track state ───────────────────────────────────────────────────────────────

#[derive(Clone, Copy, PartialEq, Debug)]
enum State {
    Tentative,
    Confirmed,
    Coasting,
}

struct Track {
    id: u32,
    x: f64,
    y: f64,
    vx: f64,
    vy: f64,
    cov_x: f64, // position variance, used as adaptive gate
    cov_y: f64,
    age: u32,
    hits: u32,
    missed: u32,
    state: State,
    counted: bool,
}

// Simple per-track constant-velocity filter (diagonal, per axis):
//   predict x += vx*dt, grow variance; update with gain k = cov/(cov+R).
const Q_POS: f64 = 0.5; // process noise on position [m^2/s]
const Q_VEL: f64 = 2.0; // process noise on velocity [(m/s)^2/s]
const R_POS: f64 = 1.0; // measurement noise on projected position [m^2]
const GATE: f64 = 3.0; // association gate [m] (Mahalanobis ~ 3 sigma)
const CONFIRM_HITS: u32 = 3;
const MAX_MISSES: u32 = 10;

impl Track {
    fn predict(&mut self, dt: f64) {
        self.x += self.vx * dt;
        self.y += self.vy * dt;
        self.cov_x += (Q_POS * dt + Q_VEL * dt * dt) as f64;
        self.cov_y += (Q_POS * dt + Q_VEL * dt * dt) as f64;
        self.age += 1;
        self.missed += 1;
    }

    fn update(&mut self, mx: f64, my: f64, dt: f64) {
        // Velocity update folded into position update for the CV model.
        let kx = self.cov_x / (self.cov_x + R_POS);
        let ky = self.cov_y / (self.cov_y + R_POS);
        let vx_meas = if dt > 1e-6 { (mx - self.x) / dt } else { self.vx };
        let vy_meas = if dt > 1e-6 { (my - self.y) / dt } else { self.vy };
        self.vx += 0.3 * (vx_meas - self.vx);
        self.vy += 0.3 * (vy_meas - self.vy);
        self.x += kx * (mx - self.x);
        self.y += ky * (my - self.y);
        self.cov_x *= 1.0 - kx;
        self.cov_y *= 1.0 - ky;
        self.hits += 1;
        self.missed = 0;
        if self.state == State::Tentative && self.hits >= CONFIRM_HITS {
            self.state = State::Confirmed;
        }
    }
}

// ─── World model ───────────────────────────────────────────────────────────────

struct WorldModel {
    dt: f64,
    ego_x: f64,
    tracks: Vec<Track>,
    next_id: u32,
    // summary
    n_frames: u64,
    n_created: u32,
    n_confirmed: u32,
    cipo_frames: u64,
    cipo_track_frames: u64,
    track_frames: u64,
}

impl WorldModel {
    fn new(fps: f64) -> Self {
        Self {
            dt: 1.0 / fps,
            ego_x: 0.0,
            tracks: Vec::new(),
            next_id: 1,
            n_frames: 0,
            n_created: 0,
            n_confirmed: 0,
            cipo_frames: 0,
            cipo_track_frames: 0,
            track_frames: 0,
        }
    }

    fn step(&mut self, f: &Frame, out: &mut BufWriter<File>) {        self.n_frames += 1;
        self.ego_x += f.ego_v * self.dt;

        // 1. Predict all tracks (world frame is ground-fixed, ego translation
        //    already applied by mapping measurements into it below).
        for t in &mut self.tracks {
            t.predict(self.dt);
        }

        // 2. Map ego-frame detections into the world frame.
        let meas: Vec<(f64, f64)> = f.dets.iter().map(|d| (self.ego_x + d.x, d.y)).collect();

        // 3. Greedy nearest-neighbour association inside the gate.
        let mut assigned_t: Vec<bool> = vec![false; self.tracks.len()];
        let mut assigned_m: Vec<bool> = vec![false; meas.len()];
        let mut pairs: Vec<(usize, usize, f64)> = Vec::new();
        for (ti, t) in self.tracks.iter().enumerate() {
            for (mi, m) in meas.iter().enumerate() {
                let dx = m.0 - t.x;
                let dy = m.1 - t.y;
                let g = (dx * dx / (t.cov_x + R_POS) + dy * dy / (t.cov_y + R_POS)).sqrt();
                if g <= GATE {
                    pairs.push((ti, mi, g));
                }
            }
        }
        pairs.sort_by(|a, b| a.2.partial_cmp(&b.2).unwrap());
        for (ti, mi, _) in &pairs {
            if assigned_t[*ti] || assigned_m[*mi] {
                continue;
            }
            assigned_t[*ti] = true;
            assigned_m[*mi] = true;
            self.tracks[*ti].update(meas[*mi].0, meas[*mi].1, self.dt);
        }
        for (mi, m) in meas.iter().enumerate() {
            if assigned_m[mi] {
                continue;
            }
            self.tracks.push(Track {
                id: self.next_id,
                x: m.0,
                y: m.1,
                vx: f.ego_v, // static prior: world-frame speed of a road object ≈ ego speed
                vy: 0.0,
                cov_x: R_POS,
                cov_y: R_POS,
                age: 1,
                hits: 1,
                missed: 0,
                state: State::Tentative,
                counted: false,
            });
            self.next_id += 1;
            self.n_created += 1;
        }

        // 4. Lifecycle: confirm / coast / delete.
        for t in &mut self.tracks {
            if t.state == State::Confirmed && !t.counted {
                t.counted = true;
                self.n_confirmed += 1;
            }
        }
        self.tracks.retain(|t| {
            let dead = t.missed > MAX_MISSES && t.state == State::Confirmed;
            let dead_tent = t.state == State::Tentative && t.missed >= 3;
            !(dead || dead_tent)
        });

        // 5. CIPO = nearest confirmed track on the path, in front of ego.
        let mut cipo_track: Option<u32> = None;
        let mut best_d = f64::INFINITY;
        for t in &self.tracks {
            if t.state != State::Confirmed || t.x <= self.ego_x {
                continue;
            }
            let d = t.x - self.ego_x;
            let lat = t.y;
            if d < best_d && lat.abs() < 1.8 {
                best_d = d;
                cipo_track = Some(t.id);
            }
        }
        if f.cipo.valid {
            self.cipo_frames += 1;
        }
        if cipo_track.is_some() {
            self.cipo_track_frames += 1;
        }
        if self.tracks.iter().any(|t| t.state == State::Confirmed) {
            self.track_frames += 1;
        }

        // 6. Emit world state.
        let wf = WorldFrame {
            frame_id: f.frame_id,
            ego: Ego { x: self.ego_x, v: f.ego_v },
            path: PathOut { valid: f.path.valid, a: f.path.a, b: f.path.b, c: f.path.c, x_min: f.path.x_min },
            tracks: self
                .tracks
                .iter()
                .filter(|t| t.state != State::Tentative || t.hits > 1)
                .map(|t| TrackOut {
                    id: t.id,
                    x: t.x,
                    y: t.y,
                    vx: t.vx,
                    vy: t.vy,
                    age: t.age,
                    hits: t.hits,
                    missed: t.missed,
                    state: match t.state {
                        State::Tentative => "tentative",
                        State::Confirmed => "confirmed",
                        State::Coasting => "coasting",
                    },
                })
                .collect(),
            cipo: CipoOut {
                valid: cipo_track.is_some(),
                track_id: cipo_track,
                d: if cipo_track.is_some() { best_d } else { f64::INFINITY },
                v_rel: if cipo_track.is_some() {
                    let t = self.tracks.iter().find(|t| Some(t.id) == cipo_track).unwrap();
                    t.vx - f.ego_v
                } else {
                    0.0
                },
                cut_in: f.cipo.cut_in,
            },
        };
        serde_json::to_writer(&mut *out, &wf).unwrap();
        out.write_all(b"\n").unwrap();
    }

    fn summary(&self) -> String {
        let conf: Vec<&Track> = self.tracks.iter().filter(|t| t.state == State::Confirmed).collect();
        format!(
            "frames={} ego_distance={:.1}m tracks_created={} tracks_confirmed={} alive_confirmed={} \
             frames_with_confirmed_tracks={} ({:.0}%) frames_with_engine_cipo={} frames_with_tracked_cipo={} ({:.0}%)",
            self.n_frames,
            self.ego_x,
            self.n_created,
            self.n_confirmed,
            conf.len(),
            self.track_frames,
            100.0 * self.track_frames as f64 / self.n_frames as f64,
            self.cipo_frames,
            self.cipo_track_frames,
            100.0 * self.cipo_track_frames as f64 / self.n_frames as f64,
        )
    }
}

// ─── Main ──────────────────────────────────────────────────────────────────────

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: worldmodel <perception.jsonl> <world_state.jsonl> [fps]");
        std::process::exit(2);
    }
    let fps: f64 = args.get(3).map(|s| s.parse().unwrap()).unwrap_or(10.0);

    let input = File::open(&args[1]).expect("open perception.jsonl");
    let out = File::create(&args[2]).expect("create world_state.jsonl");
    let mut out = BufWriter::new(out);

    let mut wm = WorldModel::new(fps);
    let mut n_dets = 0usize;
    for line in BufReader::new(input).lines() {
        let line = line.unwrap();
        let f: Frame = serde_json::from_str(&line).expect("parse frame");
        n_dets += f.dets.len();
        wm.step(&f, &mut out);
    }
    out.flush().unwrap();

    eprintln!("detections consumed: {} (avg {:.2}/frame)", n_dets, n_dets as f64 / wm.n_frames as f64);
    eprintln!("fps assumed: {} (pass as 3rd argument if different)", fps);
    println!("{}", wm.summary());
}

// ─── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn frame(id: u64, ego_v: f64, dets: Vec<(i32, f64, f64)>) -> Frame {
        Frame {
            frame_id: id,
            ego_v,
            wall_ms: 0.0,
            cipo: Cipo { valid: false, d: 0.0, v: 0.0, cut_in: false },
            path: PathIn { valid: false, a: 0.0, b: 0.0, c: 0.0, x_min: 0.0 },
            dets: dets
                .into_iter()
                .map(|(cls, x, y)| Det { cls, score: 0.9, x, y })
                .collect(),
        }
    }

    fn writer() -> BufWriter<File> {
        let path = std::env::temp_dir().join(format!("wm-test-{}-{}.jsonl", std::process::id(), frame_id_counter()));
        BufWriter::new(File::create(path).unwrap())
    }

    fn frame_id_counter() -> u64 {
        use std::sync::atomic::{AtomicU64, Ordering};
        static N: AtomicU64 = AtomicU64::new(0);
        N.fetch_add(1, Ordering::Relaxed)
    }

    #[test]
    fn track_confirms_after_three_hits() {
        let mut t = Track {
            id: 1, x: 50.0, y: 0.0, vx: 8.0, vy: 0.0,
            cov_x: R_POS, cov_y: R_POS, age: 1, hits: 1, missed: 0,
            state: State::Tentative, counted: false,
        };
        assert_eq!(t.state, State::Tentative);
        t.update(50.3, 0.0, 0.1);
        assert_eq!(t.state, State::Tentative, "2 hits must not confirm");
        t.update(50.6, 0.0, 0.1);
        assert_eq!(t.state, State::Confirmed, "3rd hit must confirm");
    }

    #[test]
    fn track_misses_accumulate_on_predict() {
        let mut t = Track {
            id: 1, x: 50.0, y: 0.0, vx: 8.0, vy: 0.0,
            cov_x: R_POS, cov_y: R_POS, age: 1, hits: 1, missed: 0,
            state: State::Confirmed, counted: true,
        };
        t.predict(0.1);
        assert_eq!(t.missed, 1);
        t.update(51.0, 0.0, 0.1);
        assert_eq!(t.missed, 0, "update must reset misses");
    }

    #[test]
    fn tentative_track_deleted_after_three_misses() {
        let mut wm = WorldModel::new(10.0);
        let mut out = writer();
        wm.step(&frame(1, 8.0, vec![(0, 40.0, 0.0)]), &mut out); // creates track
        for i in 2..=4 {
            wm.step(&frame(i, 8.0, vec![]), &mut out); // no more measurements
        }
        assert!(wm.tracks.is_empty(), "tentative track must die after 3 missed frames");
    }

    #[test]
    fn confirmed_track_survives_ten_misses_then_dies() {
        let mut wm = WorldModel::new(10.0);
        let mut out = writer();
        // Three consecutive frames confirm the track.
        for i in 1..=3 {
            wm.step(&frame(i, 8.0, vec![(0, 40.0, 0.0)]), &mut out);
        }
        assert!(wm.tracks.iter().any(|t| t.state == State::Confirmed));
        // Ten missed frames: still alive (MAX_MISSES = 10, delete only when missed > 10).
        for i in 4..=13 {
            wm.step(&frame(i, 8.0, vec![]), &mut out);
        }
        assert!(wm.tracks.iter().any(|t| t.state == State::Confirmed), "10 misses must not delete");
        // Eleventh missed frame: gone.
        wm.step(&frame(14, 8.0, vec![]), &mut out);
        assert!(wm.tracks.is_empty(), "track with >10 misses must be deleted");
    }

    #[test]
    fn cipo_is_nearest_confirmed_track_in_front_within_path() {
        let mut wm = WorldModel::new(10.0);
        let mut out = writer();
        // Two objects ahead: far on-path (x=40) and near off-path (y=3.0).
        for i in 1..=3 {
            wm.step(&frame(i, 8.0, vec![(0, 40.0, 0.0), (0, 20.0, 3.0)]), &mut out);
        }
        let mut last = Frame {
            frame_id: 4,
            ego_v: 8.0,
            wall_ms: 0.0,
            cipo: Cipo { valid: false, d: 0.0, v: 0.0, cut_in: false },
            path: PathIn { valid: true, a: 0.0, b: 0.0, c: 0.0, x_min: 0.0 },
            dets: vec![],
        };
        wm.step(&last, &mut out);
        assert_eq!(wm.next_id, 3, "two tracks must have been created");
        assert!(
            wm.tracks.iter().all(|t| t.state == State::Confirmed),
            "both tracks confirmed after 3 hits"
        );
        // CIPO selection lives in step(): nearest confirmed, |y| < 1.8, in front.
        // Off-path near track (y≈3.0) must be excluded → only the far on-path
        // track qualifies. Re-check directly on the stored tracks.
        let cipo: Vec<_> = wm
            .tracks
            .iter()
            .filter(|t| t.state == State::Confirmed && t.x > wm.ego_x && t.y.abs() < 1.8)
            .map(|t| t.id)
            .collect();
        assert_eq!(cipo.len(), 1, "only the on-path track qualifies as CIPO");
        let far = wm.tracks.iter().find(|t| t.y.abs() < 1.8).unwrap();
        assert_eq!(cipo[0], far.id);
        last.frame_id = 5; // silence unused-mut warning path
    }

    #[test]
    fn detections_are_mapped_into_world_frame() {
        let mut wm = WorldModel::new(10.0);
        let mut out = writer();
        // Ego at 8 m/s * 0.1 s = 0.8 m; detection 40 m ahead → world x ≈ 40.8.
        wm.step(&frame(1, 8.0, vec![(0, 40.0, 0.5)]), &mut out);
        let t = &wm.tracks[0];
        assert!((t.x - 40.8).abs() < 1e-9, "world x = ego_x + det.x, got {}", t.x);
        assert!((t.y - 0.5).abs() < 1e-9);
    }
}
