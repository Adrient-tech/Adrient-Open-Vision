# Training the models

Adrient OpenVision ships pretrained weights, but every head is trainable on
your own footage. This page covers the three things you can train: the
detector (YOLO11n), the lane/drivable-area head, and the BEV segmentation
network.

## 0. Prepare a dataset (all tasks)

1. Record footage with the camera rig you will actually use (see
   docs/camera-setup.md). 10–30 minutes of driving covers most cases.
2. Split into `train/` and `val/` (90/10), keeping whole drives out of one
   side to avoid near-duplicate frames.
3. Label with any YOLO-format tool (CVAT, Label Studio, Roboflow export).

## 1. Fine-tune the YOLO11n detector

The shipped `yolo11n.onnx` was pretrained on COCO (80 classes). For dashcam
work fine-tune on your classes, e.g. `car, truck, bus, motorcycle, bicycle,
person, traffic light, stop sign`:

```bash
pip install ultralytics

yolo detect train \
  model=yolo11n.pt \
  data=your_dataset.yaml \
  imgsz=1024 \
  epochs=50 \
  batch=16
```

`your_dataset.yaml`:

```yaml
path: /path/to/dataset
train: images/train
val: images/val
names:
  0: car
  1: truck
  2: bus
  3: motorcycle
  4: bicycle
  5: person
  6: traffic light
  7: stop sign
```

Notes:

- `imgsz=1024` matches the engine's wide input; the engine resizes to
  512×1024 so square 1024 training transfers cleanly.
- Train on a GPU machine (no GPU here — rent one via vastai/runpod/modal, or
  use Google Colab; an 8 GB card is enough for yolo11n).
- If you use custom classes, update `class_kind()` in
  `worldmodel/src/bin/adapter.rs` so each class id maps to the right viewer
  kind and footprint, then rebuild the adapter.

Export to ONNX at the engine's exact input size:

```bash
python -c "from ultralytics import YOLO; YOLO('runs/detect/train/weights/best.pt').export(format='onnx', imgsz=[512,1024], opset=17)"
cp runs/detect/train/weights/best.onnx engine/modules/models/weights/yolo11n.onnx
```

Verify: `bash scripts/run_pipeline.sh` and check the class mix printed by the
scene adapter.

## 2. Fine-tune the lane / drivable-area head

The `autodrive_*.onnx` head segments lane lines and drivable area. To
retrain it on your roads:

1. Label binary masks (drivable area, lane lines) for ≥ 500 frames — the
   Label Studio "brush" mode works.
2. Train a small U-Net/DeepLab-style model in PyTorch at 512×1024.
3. Export with a fixed output `[1, num_classes, 512, 1024]` after sigmoid:

   ```python
   torch.onnx.export(model.eval(), dummy, "autodrive_fp32.onnx",
                     input_names=["images"], output_names=["output"],
                     opset_version=17)
   ```

4. Copy to `engine/modules/models/weights/` and rebuild.

## 3. Train a BEV segmentation network (BEV setup)

For the BEV camera type (docs/camera-setup.md §3):

1. Collect frames from the multi-camera rig with known intrinsics/extrinsics.
2. Project LiDAR ground truth (or hand-drawn BEV masks) into a 200×200 BEV
   grid at 0.5 m/cell — classes: road, lane line, divider, obstacle.
3. Train a Lift-Splat-Shoot or YOLOPv2-BEV variant on your rig; input is the
   stacked camera images + calibrated extrinsics.
4. Export to ONNX, add to `engine/modules/models/weights/`, and wire the
   output grid into the viewer's road rendering the same way the fitted lane
   ribbon is fed today.

## 4. Validate before shipping

```bash
yolo val model=yolo11n.onnx data=your_dataset.yaml imgsz=1024   # mAP
bash scripts/run_pipeline.sh                                     # end-to-end
cd worldmodel && cargo test --release                            # tracker
```

Track-quality targets on a 10 fps dashcam stream: ≥ 95% of frames have
confirmed tracks, average track length > 5 s, ID switches < 5%.
