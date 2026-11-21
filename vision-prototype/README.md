# BSP Vision — C++ desktop prototype

A standalone Qt 6 / C++20 Windows application for offline BSP inspection and visual-matching experiments. It does not read game-process memory, inject into a game, automate mouse input, or control gameplay.

## What is implemented

- Qt desktop UI with **Visual matching** and **BSP geometry** tabs.
- Load two image files and compare local ORB visual features.
- Ratio-test descriptor filtering and RANSAC homography verification; the UI reports the inlier count separately from raw matches.
- Load Source **BSP version 20** files with validation of the VBSP signature, 64 lump ranges, and core geometry record lengths.
- Parse vertices, edges, surfedges, and faces; reconstruct valid face outlines.
- Preview the map from above in XY, click to inspect XY coordinates, and inspect a table of lump sizes and record counts.
- Local-only processing; no neural-model files are downloaded or required.

## Important limitations

The current feature matcher is ORB, a classical computer-vision baseline rather than a neural network. RANSAC homography verification is useful for image-to-image matching, but it is **not** a 3D camera-pose estimate.

The BSP preview is a top-down outline view. A click reports X/Y only: Z is intentionally not guessed because several floors or ceilings can share the same XY position. BSP v20 is the only supported version in this prototype. Materials and textures are not rendered.

The next planned increments are user-selected screen capture, optional learned matching via ONNX Runtime, 2D-to-3D landmark association, camera-intrinsic calibration, and confidence-checked pose estimation.

## Build on Windows

Prerequisites:

- Visual Studio 2022 with the **Desktop development with C++** workload.
- CMake 3.21 or newer.
- Qt 6.5 or newer (MSVC 2022 64-bit kit; Widgets component).
- OpenCV 4.8 or newer, including core, imgproc, imgcodecs, features2d, and calib3d.

Open a **Developer PowerShell for VS 2022**, change to the repository root, and adjust the Qt/OpenCV paths to match your installation:

~~~powershell
cd vision-prototype
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" -DOpenCV_DIR="C:/opencv/build"
cmake --build build --config Release
.\build\Release\bsp-vision.exe
~~~

If you already opened the repository root in Visual Studio, use **File → Open → CMake…** and select vision-prototype/CMakeLists.txt. Make sure Visual Studio is using the x64 configuration and that CMake can find both Qt and OpenCV.

If CMake reports that it cannot find OpenCVConfig.cmake, set OpenCV_DIR to the directory that contains that file, not merely the top-level OpenCV source folder.

## Using the app

1. In **BSP geometry**, choose a Source BSP v20 file. Check the summary and lump table; click the top-down preview to read XY.
2. In **Visual matching**, load a reference image and a screenshot of the same scene from a different view.
3. Choose **Find visual matches**. Inspect the drawn match lines and RANSAC inlier count. Treat this as a diagnostic, not a reliable camera pose.

For meaningful image matching, use screenshots from the same map and preferably the same material/lighting configuration.

## Development notes

- Main entry point: src/main.cpp
- BSP header/range validation and geometry extraction: src/bsp_file.cpp
- Top-down map rendering and XY picking: src/bsp_map_view.cpp
- Build configuration: CMakeLists.txt

No build was executed in the GitHub editing environment; compile and runtime validation still need to be done on a Windows machine with Qt and OpenCV installed.
