# BSP Vision — C++ prototype

This is the first desktop prototype for visual localization. It lets you load a reference image and a screenshot, then inspect local visual-feature matches. It does not read the Garry's Mod process, automate input, or control the game.

## Current scope

- Qt 6 desktop UI.
- Load two local images.
- OpenCV ORB keypoint detection and descriptor matching.
- Ratio-test filtering and a match visualization.
- Fully local operation; no model download or network request is needed.

ORB is a lightweight baseline, not the final learned matcher. A future optional ONNX Runtime backend (for example, SuperPoint + LightGlue) can be added behind the same matching interface. Camera pose estimation is not implemented yet: it requires camera intrinsics and reliable 2D-to-3D correspondences.

## Build on Windows

Install Visual Studio 2022 with the C++ desktop workload, CMake, Qt 6.5+ (MSVC kit), and OpenCV 4.8+.

Configure CMake with paths for your Qt and OpenCV installations. Adjust these example paths for your machine:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" -DOpenCV_DIR="C:/opencv/build"
cmake --build build --config Release
.`buildReleasesp-vision.exe
```

You can also open this directory as a CMake project in Qt Creator.

## Planned next steps

1. Add automated tests and a repeatable sample-image evaluation set.
2. Add Windows screen capture with explicit user selection of a window or monitor.
3. Add a BSP geometry adapter and coordinate-system validation against the existing browser viewer.
4. Add optional ONNX Runtime learned feature extraction and matching, with model files supplied separately and license notices included.
5. Add robust pose estimation only after 2D-to-3D correspondences, camera intrinsics, and confidence checks are available.

Do not treat raw match count as a localization confidence score. False matches must be rejected geometrically before any pose estimate is trusted.
