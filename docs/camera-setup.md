# Camera setup

How to set up cameras for Adrient OpenVision: single dashcam, multi-camera
rings, and BEV (bird's-eye-view) rigs. All three feed the same perception
engine — only the calibration differs.

## Concepts

The engine needs to convert **pixels → world metres**. That conversion is the
homography `H` (a 3×3 matrix) plus the depth analyser. Camera setup = mounting
the camera(s) so `H` can be estimated accurately.

- **Intrinsics** — focal length, principal point, distortion. One calibration
  per physical camera model, done once with a chessboard.
- **Extrinsics** — where the camera sits on the vehicle (height, pitch, yaw).
  Estimated from one calibration drive or a chessboard on the ground.

## 1. Single camera (dashcam)

The default setup, and what the sample dataset uses.

1. Mount the camera behind the windshield, centred, horizontal, roughly 1.2–1.5 m
   above the road. Any pitch between 0° (level) and about −10° works; keep it
   fixed afterwards — the homography depends on it.
2. Record a short clip of the road with at least 4 lane-line intersections or a
   zebra crossing (a straight, empty car park with chalk lines is ideal).
3. Calibrate intrinsics with OpenCV chessboard images (9×6 inner corners,
   ~25 mm squares):

   ```bash
   python3 scripts/calibrate_intrinsics.py --images calib/*.jpg --out camera.yaml
   ```

4. Estimate the homography `H` from ≥ 4 ground points whose world positions you
   measured (e.g. lane-line corners). `engine/Calibration/homography_C.yaml`
   holds the sample; edit the points and re-run the estimator, or use the
   `Calibration/` tools.
5. Verify: run the pipeline and check that a car parked exactly 20 m away
   reports ≈ 20 m in the HUD. Small global scale errors are normal; correct
   them by re-measuring one ground point.

Single-camera limits (why BEV / multi-cam exist): no side coverage, depth for
off-plane objects relies on MiDaS, and lane fitting degrades on sharp curves.

## 2. Multiple cameras (dashcam + 360° ring)

For side and rear coverage — overtaking vehicles, cut-ins from adjacent lanes,
crossings.

- Rig: one front dashcam + 2 side cameras (mirrors) + 1 rear, or 4× 190° fisheye
  at the mirrors/roof corners.
- **Calibrate each camera independently** (steps 3–4 above). Each gets its own
  `camera_<name>.yaml` and `homography_<name>.yaml`.
- Sync: hardware trigger or PTP is best; with USB cameras keep all cameras on
  one controller and accept ≤ 1 frame skew. The world model tolerates small
  skew because tracks persist (confirm at 4 hits, delete at 10 misses).
- Fusion: each camera produces detections in its own world frame; the world
  model (`worldmodel/`) merges them automatically — every detection enters the
  same nearest-neighbour association gate, so an object seen by two cameras
  keeps one track ID.
- Recommended order: bring up the front camera alone first (section 1), then
  add cameras one at a time, checking each in the 3D viewer before adding the
  next.

## 3. BEV type (top-down reconstruction)

BEV is what you see in Tesla-style renders: the road, lanes and all objects
rebuilt in a top-down world instead of per-camera boxes. Two ways to get it:

**a) Homography-stitched BEV (CPU, already supported).** Project each camera's
segmentation (drivable area, lane lines) through its homography into the
ground plane and mosaic them. Works with the rigs above; quality drops more
than ~15 m ahead because ground pixels get thin.

**b) Learned BEV (recommended for real simulation).** A BEV segmentation
network (e.g. YOLOPv2-style or a Lift-Splat-Shoot variant) predicts drivable
area, lanes and dividers directly in top-down space from one or more images.
Train it on your calibrated rig (see docs/training.md); export to ONNX and add
it to `engine/modules/models/weights/`. The output BEV grid feeds the viewer's
road rendering the same way the fitted lane ribbon does today.

Practical camera requirements for learned BEV: known intrinsics, known
extrinsics per camera, overlapping fields of view between adjacent cameras,
and consistent exposure/white balance across cameras.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| All distances scaled by a constant | Wrong homography scale point | Re-measure one ground point, re-estimate H |
| Cars off-road or rotated on curves | H fit on straight road only | Calibrate on a section with mild curvature |
| Ghost tracks at image edges | Side camera yaw drift | Re-tighten mount, re-calibrate extrinsics |
| Objects flicker between cameras | Exposures differ | Lock exposure/WB on all cameras |
