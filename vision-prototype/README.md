# BSP Geometry & Ballistics

A standalone Windows desktop application written in C++20 and Qt 6 for inspecting Source BSP v20 maps and exploring ideal projectile trajectories. All map parsing and calculations are performed locally. The app does not read game-process memory, inject into a game, automate mouse input, or control gameplay.

## Features

- **BSP geometry:** load Source BSP version 20 files, validate the VBSP signature and lump ranges, and inspect a summary of lump sizes and record counts.
- **Map parsing:** extract vertices, edges, surfedges, and faces to reconstruct valid face outlines.
- **Geometry views:** inspect face outlines from above in XY, select XY coordinates by clicking the map, and rotate/zoom a lightweight 3D preview with the mouse.
- **Ballistic lab:** enter launch and target XYZ coordinates and configure projectile speed, gravity, yaw, elevation, and flight time.
- **Map coordinate picking:** pick launch or target X/Y from the BSP preview. Enter Z manually because a top-down view cannot determine which floor or ceiling a point belongs to.
- **Trajectory solving:** calculate the current trajectory or solve for a low-arc/high-arc ideal projectile angle to a target.
- **Trajectory display:** see the path in XY plan, XZ elevation, and oblique 3D views, with launch/target markers, grids, and numeric axis labels.
- **Parser tests:** synthetic tests cover BSP header handling, geometry reconstruction, and rejection of invalid signatures or lump ranges.

## Limitations

The parser currently supports BSP version 20 only. Materials and textures are not rendered. The 3D view is a lightweight painter-based projection rather than a full z-buffer renderer, so intersecting or back-facing surfaces may overlap imperfectly.

The ballistic lab uses ideal constant-gravity projectile equations. It does not simulate collisions, map occlusion, air drag, splash radius, weapon-specific behavior, or server-side physics. Treat computed paths as a visual planning reference; they are not automatically sent to a game.

## Build on Windows

Prerequisites:

- Visual Studio 2022 with the **Desktop development with C++** workload.
- CMake 3.21 or newer.
- Qt 6.5 or newer with the MSVC 2022 64-bit **Widgets** component.

Open **Developer PowerShell for VS 2022**, then configure and build. Adjust the Qt path to match your installation:

~~~powershell
cd vision-prototype
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\bsp-geometry.exe
~~~

If you use Visual Studio's CMake workflow, choose **File → Open → CMake…** and select vision-prototype/CMakeLists.txt. Ensure the selected configuration is x64 and that CMake can locate Qt.

## Using the app

1. In **BSP geometry**, load a Source BSP v20 file and inspect the summary and lump table.
2. Use **Top-down XY** to view the map outline and click to inspect coordinates, or use **Orbit 3D** to rotate the geometry (drag) and zoom (mouse wheel).
3. In **Ballistic lab**, enter launch and target XYZ coordinates and adjust the projectile parameters. Use **Pick XY on map…** to select X/Y from the BSP preview, then enter Z manually.
4. Choose **Calculate current arc** to preview the entered yaw/elevation or **Solve target · low arc** / **Solve target · high arc** to calculate an ideal angle for reaching the selected target.

## Development notes

- Main UI and ballistic calculations: src/main.cpp
- BSP header/lump validation and geometry extraction: src/bsp_file.cpp
- Top-down map rendering and XY picking: src/bsp_map_view.cpp
- Orbitable 3D preview: src/bsp_3d_view.cpp
- Trajectory drawing: src/ballistics_view.cpp
- Parser tests: tests/bsp_file_tests.cpp

The GitHub Actions workflow builds the Windows application and runs the BSP parser tests.
