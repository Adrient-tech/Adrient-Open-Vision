# Changelog

All notable user-facing changes to Adrient OpenVision.

## Unreleased

### Added
- YOLO11n ONNX detector (80 COCO classes): cars, trucks, buses, motorcycles,
  bicycles, pedestrians, traffic lights and stop signs are now detected as
  separate classes instead of a generic "vehicle".
- Class-aware 3D rendering: distinct viewer colour and footprint per class
  (car, truck, bus, motorcycle, bicycle, person) in `viewer/index.html`.
- `models/` directory: model inventory (`models/README.md`) and download
  script `models/download_models.sh`.
- Documentation: `docs/camera-setup.md` (single camera, multi-camera, BEV
  type, calibration), `docs/cpu-setup.md`, `docs/gpu-setup.md` (CUDA ONNX
  Runtime, remote GPU hosts), `docs/training.md` (detector, lane head and
  BEV network training).
- `scripts/run_pipeline.sh` now rebuilds the scene adapter so adapter changes
  reach `viewer/scene.json` without a manual `cargo build`.

### Changed
- Detector weights switched from `autospeed_*.onnx` to `yolo11n.onnx`
  (same `[1, 84, N]` output contract, already-sigmoided probabilities).
- `AutoSpeed` post-processing gained a `sigmoid_logits` flag; raw-logit
  models (autospeed) and probability models (YOLO) both work.
- Model search path now also resolves `engine/modules/models/weights/`
  relative to the repository root when running from `engine/build`.
