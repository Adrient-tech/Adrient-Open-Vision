#!/usr/bin/env python3
"""Offline 3D reconstruction: dashcam video → fused, coloured point cloud.

Converts the camera images into actual 3D geometry (the "Tesla-style" world
layer): every sampled frame is run through a monocular-depth ONNX model
(MiDaS-small, or Depth Anything V2 small on the mid/top tiers — the model is
picked by its input tensor name), calibrated against the ground-plane
homography exactly like the engine's DepthAnalyser
(engine/modules/models/src/depth_analyser.cpp), then back-projected to metric 3D
and fused across frames using the ego odometry from world_state.jsonl.

Vertical model: for a level camera, height along a pixel ray is linear in the
horizontal reach — height = h * (1 - R / R_ground), where R_ground is the
ground-plane reach the homography gives that pixel. Ground pixels therefore
reconstruct at height 0 by construction, and elevated content (vehicles,
signs) rises above the road.

Tier mid/top (--road-mask <ddrnet23_slim.onnx>) additionally runs DDRNet23-Slim
road segmentation per frame and keeps near-ground points only where the model
actually sees road/sidewalk — no more "phantom" ground geometry on grass or
barriers, and nothing is invented that the camera did not see.

Usage:
  python3 scripts/reconstruct3d.py <input.mp4> <world_state.jsonl> \
      <H.yaml> <depth.onnx> <out_dir> [frame_stride] [voxel_m] \
      [--road-mask <ddrnet23_slim.onnx>]

Outputs (in <out_dir>):
  reconstruction.bin  — interleaved float32 x,y,z + uint8 r,g,b per point
  reconstruction.json — { count, stride } header for the viewer loader
"""

import argparse
import json
import re
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

CAMERA_HEIGHT_M = 1.5   # dashcam height above the road; scales elevation only
MIN_REACH_M = 2.0
MAX_REACH_M = 110.0
MIN_H_M, MAX_H_M = -0.4, 10.0
# DDRNet23-Slim (Cityscapes 19 classes): pixels in these classes are ground the
# camera really sees; near-ground points outside them are dropped.
ROAD_CLASSES = (0, 1)  # road, sidewalk


def load_H(path):
    text = Path(path).read_text()
    data = re.search(r"data:\s*\[([^\]]+)\]", text).group(1)
    return np.array([float(v) for v in data.split(",")], dtype=np.float64).reshape(3, 3)


def load_ego_x(path):
    ego = {}
    with open(path) as fh:
        for line in fh:
            f = json.loads(line)
            ego[int(f["frame_id"])] = float(f["ego"]["x"])
    return ego


def build_depth_session(path):
    """Load a monocular-depth ONNX model; detect its flavour by input name."""
    sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    inp = sess.get_inputs()[0]
    shape = inp.shape
    if inp.name == "pixel_values":          # Depth Anything V2 (transformers export)
        n = shape[2] if isinstance(shape[2], int) else 518
        return sess, inp, n, "depth-anything-v2"
    n = shape[2] if isinstance(shape[2], int) else 384   # MiDaS-small
    return sess, inp, n, "midas"


def build_road_session(path):
    """DDRNet23-Slim Cityscapes segmentation: fixed 1x3x1024x2048 RGB input."""
    sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    return sess, sess.get_inputs()[0]


def road_mask_grid(seg_sess, seg_inp, frame, n):
    """Run segmentation on one frame, return a bool grid (n/n step resolution)."""
    h, w = seg_inp.shape[2], seg_inp.shape[3]
    rgb = cv2.cvtColor(cv2.resize(frame, (w, h)), cv2.COLOR_BGR2RGB)
    x = (rgb.astype(np.float32) / 255.0).transpose(2, 0, 1)[None]
    logits = seg_sess.run(None, {seg_inp.name: x})[0]   # (1, 19, h, w)
    mask = logits[0].argmax(axis=0)
    ground = np.isin(mask, ROAD_CLASSES)
    return cv2.resize(ground.astype(np.uint8), (n, n), interpolation=cv2.INTER_NEAREST) > 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("video")
    ap.add_argument("world_state")
    ap.add_argument("h_yaml")
    ap.add_argument("depth_model")
    ap.add_argument("out_dir")
    ap.add_argument("frame_stride", nargs="?", type=int, default=2)
    ap.add_argument("voxel_m", nargs="?", type=float, default=0.35)
    ap.add_argument("--road-mask", metavar="DDRNAET_ONNX", default=None,
                    help="DDRNet23-Slim ONNX: keep near-ground points only on road/sidewalk")
    args = ap.parse_args()
    stride, voxel = args.frame_stride, args.voxel_m

    H = load_H(args.h_yaml)
    ego_x = load_ego_x(args.world_state)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    sess, inp, N, flavour = build_depth_session(args.depth_model)
    seg_sess, seg_inp = (build_road_session(args.road_mask)
                         if args.road_mask else (None, None))
    print(f"depth model: {flavour}, input {N}x{N}"
          + (", road mask on" if seg_sess is not None else ""), flush=True)

    cap = cv2.VideoCapture(args.video)
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    Hgt = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    # Pixel grid at depth-model resolution, subsampled 2x, mapped to frame pixels.
    step = 2
    vs, us = np.mgrid[0:N:step, 0:N:step]
    fu = (us + 0.5).astype(np.float32) * (W / N)
    fv = (vs + 0.5).astype(np.float32) * (Hgt / N)
    grid = np.stack([fu, fv], axis=-1).reshape(-1, 1, 2)
    ground = cv2.perspectiveTransform(grid, H).reshape(-1, 2)  # (x_fwd, y_lat)
    R_ground = np.linalg.norm(ground, axis=1)
    ex, ey = ground[:, 0], ground[:, 1]
    gvalid = np.isfinite(R_ground) & (R_ground > MIN_REACH_M) & (R_ground < MAX_REACH_M)

    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)

    pts_all, col_all = [], []
    fid = 0
    n_frames_used = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        if fid % stride == 0 and fid in ego_x:
            rgb = cv2.cvtColor(cv2.resize(frame, (N, N)), cv2.COLOR_BGR2RGB)
            x = ((rgb.astype(np.float32) / 255.0) - mean) / std
            x = x.transpose(2, 0, 1)[None]
            pred = sess.run(None, {inp.name: x})[0].reshape(-1)
            if pred.size != N * N:
                # MiDaS-style patch-grid output → back to the sampling grid.
                pred = cv2.resize(pred.reshape(N // 14, N // 14), (N, N))
            pred = pred.reshape(N, N)

            ground_ok = (road_mask_grid(seg_sess, seg_inp, frame, N)[::step, ::step].reshape(-1)
                         if seg_sess is not None else None)
            pv = pred[::step, ::step].reshape(-1)
            ok_g = gvalid & (pv > 1e-6)
            if ok_g.sum() > 100:
                k = float(np.median(R_ground[ok_g] * pv[ok_g]))  # dist ≈ k / pred
                R = k / np.maximum(pv, 1e-6)
                height = CAMERA_HEIGHT_M * (1.0 - R / np.maximum(R_ground, 1e-6))
                keep = (
                    gvalid
                    & (pv > 1e-6)
                    & (R > MIN_REACH_M)
                    & (R < MAX_REACH_M)
                    & (height > MIN_H_M)
                    & (height < MAX_H_M)
                )
                if ground_ok is not None:
                    # Near-ground geometry must sit on real road/sidewalk pixels;
                    # elevated content (vehicles, signs) is kept regardless.
                    near_ground = (height < 0.5) & keep
                    keep = keep & (~near_ground | ground_ok)
                X = ego_x[fid] + R[keep] * (ex[keep] / np.maximum(R_ground[keep], 1e-6))
                Y = ey[keep]  # lateral straight from the ground homography
                Z = height[keep]
                finite = np.isfinite(X) & np.isfinite(Y) & np.isfinite(Z)
                X, Y, Z = X[finite], Y[finite], Z[finite]
                colors = rgb[vs.reshape(-1)[keep], us.reshape(-1)[keep]][finite]
                if len(X) == 0:
                    fid += 1
                    continue
                pts_all.append(np.stack([X, Y, Z], axis=1).astype(np.float32))
                col_all.append(colors.astype(np.uint8))
                n_frames_used += 1
        fid += 1
        if fid % 50 == 0:
            print(f"frame {fid} …", flush=True)
    cap.release()

    pts = np.concatenate(pts_all)
    cols = np.concatenate(col_all)
    print(f"{len(pts)} raw points from {n_frames_used} frames", flush=True)

    # Voxel-grid downsample: average colour per occupied voxel.
    key = np.floor(pts / voxel).astype(np.int64)
    _, idx, inv, counts = np.unique(
        key, axis=0, return_index=True, return_inverse=True, return_counts=True)
    sums = np.zeros((len(idx), 3), dtype=np.int64)
    np.add.at(sums, inv, cols)
    avg = (sums / counts[:, None]).astype(np.uint8)
    pts, cols = pts[idx], avg

    print(f"{len(pts)} points after {voxel} m voxel downsample", flush=True)

    inter = np.empty(len(pts), dtype=np.dtype([
        ("xyz", np.float32, 3), ("rgb", np.uint8, 3), ("pad", np.uint8, 1)]))
    inter["xyz"] = pts
    inter["rgb"] = cols
    (out_dir / "reconstruction.bin").write_bytes(inter.tobytes())
    (out_dir / "reconstruction.json").write_text(json.dumps({
        "count": int(len(pts)),
        "point_bytes": 16,
        "voxel_m": voxel,
        "camera_height_m": CAMERA_HEIGHT_M,
        "frames_used": n_frames_used,
        "depth_model": flavour,
        "road_mask": seg_sess is not None,
    }))
    print(f"wrote {out_dir}/reconstruction.bin ({(out_dir / 'reconstruction.bin').stat().st_size/1e6:.1f} MB)")


if __name__ == "__main__":
    main()
