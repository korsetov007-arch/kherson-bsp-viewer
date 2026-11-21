# Localization architecture

## Pipeline

1. Capture an image through a user-selected Windows capture source.
2. Extract visual features from the screenshot and reference imagery.
3. Match descriptors and reject outliers with geometric verification.
4. Associate verified image features with 3D landmarks reconstructed from the BSP.
5. Estimate camera pose only when camera intrinsics and enough well-distributed 2D-to-3D correspondences are available.
6. Show pose, residuals, inlier count, and failure state in the UI.

## Important constraints

- BSP geometry alone does not contain the original rendered appearance of every surface. Materials and texture assets improve visual matching substantially.
- A reference screenshot without a known camera pose is useful for image-to-image matching, but does not by itself create a 2D-to-3D landmark map.
- The current ORB baseline is not a neural model and does not estimate camera pose.
- All uncertain estimates should be surfaced as uncertain. Do not infer a pose from match count alone.

## Validation plan

Use screenshots captured from a controlled local test scene. Hold out camera positions that were not used to build the reference set. Measure match inliers, reprojection error, and position/orientation error against known ground truth. Add the ONNX learned matcher only after baseline evaluation is repeatable.
