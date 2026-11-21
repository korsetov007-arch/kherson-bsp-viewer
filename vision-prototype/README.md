# BSP Vision — C++ desktop prototype

A standalone Qt 6 / C++20 Windows application for offline BSP inspection and visual-matching experiments. It does not read game-process memory, inject into a game, automate mouse input, or control gameplay.

## What is implemented

- Qt desktop UI with **Visual matching**, **BSP geometry**, and **Ballistic lab** tabs.
- Load two image files and compare local ORB visual features.
- Capture the primary monitor after a two-second delay; the app hides temporarily so the user can switch back to the game, without reading game memory or automating controls.
- Ratio-test descriptor filtering and RANSAC homography verification; the UI reports the inlier count separately from raw matches.
- Load Source **BSP version 20** files with validation of the VBSP signature, 64 lump ranges, and core geometry record lengths.
- Parse vertices, edges, surfedges, and faces; reconstruct valid face outlines.
- Preview face outlines from above in XY, click to inspect XY coordinates, orbit a simple painter-based 3D view with drag-to-rotate and wheel-to-zoom, and inspect a table of lump sizes and record counts.\n- Ballistic lab with launch and target XYZ entry, map-click XY picking, configurable initial speed/gravity/yaw/elevation/time, low-arc and high-arc target solvers, and a three-panel trajectory display (XY plan, XZ elevation, oblique 3D) with launch/target markers, grid lines and axis labels.
- Synthetic BSP parser tests check header handling, geometry reconstruction, and rejection of invalid signature/lump ranges.
- Local-only processing; no neural-model files are downloaded or required.

## Important limitations

The current feature matcher is ORB, a classical computer-vision baseline rather than a neural network. RANSAC homography verification is useful for image-to-image matching, but it is **not** a 3D camera-pose estimate.

The BSP preview has a top-down XY outline mode and a lightweight painter-based 3D orbit mode. A click reports X/Y only: Z is intentionally not guessed because several floors or ceilings can share the same XY position. BSP v20 is the only supported version in this prototype. Materials and textures are not rendered.

The ballistic lab uses ideal constant-gravity projectile equations. It does not model collisions, map occlusion, air drag, splash radius, weapon-specific behavior or server-side physics. Tune speed and gravity against the intended offline test environment; calculated angles are a visual reference and are not automatically sent to a game. The lightweight 3D view is a projection for inspection, not a full z-buffer renderer.\n\nThe next planned increments are optional learned matching via ONNX Runtime, 2D-to-3D landmark association, camera-intrinsic calibration, and confidence-checked pose estimation.

## Build on Windows

Prerequisites:

- Visual Studio 2022 with the **Desktop development with C++** workload.
- CMake 3.21 or newer.
- Qt 6.5 or newer (MSVC 2022 64-bit kit; Widgets component).
- Either OpenCV 4.8+ installed locally, or vcpkg to install OpenCV from the included manifest.

### Option A — install OpenCV with vcpkg

Open **Developer PowerShell for VS 2022**. If you do not already have vcpkg, clone and bootstrap it once:

~~~powershell
git clone https://github.com/microsoft/vcpkg C:/dev/vcpkg
& C:/dev/vcpkg/bootstrap-vcpkg.bat
~~~

Then, from the repository root, run the configure/build commands below. Change the Qt and vcpkg paths to match your machine:

~~~powershell
cd vision-prototype
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\bsp-vision.exe
~~~

The included vcpkg manifest requests OpenCV's calibration module and uses the standard OpenCV modules required by this app. On the first configure, vcpkg may take some time to build dependencies.

### Option B — use an existing OpenCV installation

If OpenCV is already installed, configure without a vcpkg toolchain and point OpenCV_DIR at the directory containing OpenCVConfig.cmake:

~~~powershell
cd vision-prototype
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" -DOpenCV_DIR="C:/opencv/build"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\bsp-vision.exe
~~~

If you already opened the repository root in Visual Studio, use **File → Open → CMake…** and select vision-prototype/CMakeLists.txt. Ensure the selected configuration is x64 and that CMake can find both Qt and OpenCV.

## Using the app

1. In **BSP geometry**, choose a Source BSP v20 file. Check the summary and lump table. Use **Top-down XY** for coordinate inspection or **Orbit 3D** to rotate the geometry (drag) and zoom (wheel).
2. In **Visual matching**, load a reference image. To grab the game display, choose **Capture primary screen (2 s)** and switch to the game after the app hides; otherwise load a screenshot file.
3. Choose **Find visual matches**. Inspect the drawn match lines and RANSAC inlier count. Treat this as a diagnostic, not a reliable camera pose.\n4. In **Ballistic lab**, enter launch XYZ and projectile parameters. Use **Pick XY on map…** to choose launch or target XY from the BSP preview; enter Z manually because the top-down view cannot identify the floor. Choose **Calculate current arc** to preview the entered angles or **Solve target · low arc/high arc** to estimate an ideal elevation angle. The amber path is shown in XY, XZ and oblique 3D views.

Capture currently targets the primary monitor. Some exclusive-fullscreen games may not be capturable; try borderless/windowed mode in that case.

For meaningful image matching, use screenshots from the same map and preferably the same material/lighting configuration.

## Development notes

- Main entry point: src/main.cpp
- BSP header/range validation and geometry extraction: src/bsp_file.cpp
- Top-down map rendering and XY picking: src/bsp_map_view.cpp
- Parser tests: tests/bsp_file_tests.cpp
- Build configuration and dependency manifest: CMakeLists.txt and vcpkg.json

A Windows GitHub Actions workflow builds the app and runs the parser tests. The first successful CI run should be treated as the compile verification; local runtime behavior still needs to be checked with representative BSP files and screenshots.
