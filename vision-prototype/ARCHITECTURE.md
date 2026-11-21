# Localization architecture and current status

## Implemented baseline

- A C++20 / Qt 6 desktop shell with two tabs.
- ORB image features, descriptor ratio filtering, and RANSAC homography inlier visualization.
- Source BSP v20 header/lump validation plus extraction of vertices, edges, surfedges, and face outlines.
- Top-down XY geometry preview, orbitable 3D face preview, and a full lump summary table.
- Delayed primary-monitor screenshot capture using Qt screen APIs.\n- Ballistic trajectory preview with configurable projectile parameters, map-picked/manual launch and target coordinates, low/high arc solving, and XY/XZ/oblique 3D displays with axis guides.

## Intended localization pipeline

1. Capture an image through a user-selected Windows capture source.
2. Extract features from the screenshot and reference imagery.
3. Match descriptors and reject outliers with geometric verification.
4. Associate verified image features with 3D landmarks reconstructed from the BSP.
5. Estimate camera pose only when camera intrinsics and enough well-distributed 2D-to-3D correspondences are available.
6. Show pose, residuals, inlier count, and explicit failure/uncertainty states in the UI.

## Important constraints\n\n- Ballistic calculation currently uses ideal constant-gravity equations; collisions, map occlusion, drag and game-specific projectile physics are not simulated. It is a visual planning aid and does not automate game input.

- The 3D painter preview is not a z-buffer renderer; intersecting or back-facing surfaces may overlap imperfectly.

- The current ORB baseline is not a neural model and does not estimate camera pose.
- Image-to-image homography is appropriate mainly for planar or approximately planar regions; it must not be presented as a 3D pose.
- BSP geometry alone does not contain the rendered appearance of every surface. Materials and texture assets improve visual matching substantially.
- A reference screenshot without a known camera pose does not by itself provide a 2D-to-3D landmark map.
- In a top-down view, one XY location may correspond to multiple floor/ceiling heights. The application therefore reports XY only instead of guessing Z.
- All uncertain estimates should be surfaced as uncertain. Do not infer a pose from match count alone.

## Validation plan

Use screenshots captured from a controlled local test scene. Hold out camera positions that were not used to build the reference set. Measure match inliers, reprojection error, and position/orientation error against known ground truth. Add an ONNX learned matcher only after baseline evaluation is repeatable.
