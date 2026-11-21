# Source BSP Geometry Lab

Bilingual Windows desktop tool for inspecting Source Engine BSP v20 geometry and planning ideal projectile trajectories. Built with C++20 and Qt 6; it does not require OpenCV or any computer-vision model.

## Features

- Load a Source BSP v20 map and inspect its lump table.
- View polygon geometry from above (XY) or in a rotatable 3D projection.
- Detect common Source skybox material names from BSP texture metadata. Sky-textured faces are hidden by default so giant sky planes are less likely to overwhelm the level preview; use **Show skybox surfaces** to inspect them.
- Omit non-renderable Source tool surfaces such as triggers, clips and nodraw faces from the geometry preview.
- Switch the interface between Russian and English via **Settings → Language**. The selected language is saved for the next launch.
- Select X/Y coordinates by clicking the map. Enter Z manually to choose the correct floor.
- Plan a projectile path in XY, XZ and oblique 3D views, and solve for low or high ideal ballistic arcs.
- Run synthetic BSP parser tests with CTest.
- Optional Tier B screen telemetry from `cl_showpos`: DXGI Desktop Duplication capture plus pixel-template matching, with crop controls, range checks and a confidence score. No OCR engine or neural-network model is used.
- On-demand displacement-aware two-layer heightfield (`ground`/`canopy`), hillshade and cached 128/256-unit contour lines in the 2D preview.
- A median-split triangle BVH clips calculated trajectories at the first BSP render-surface intersection, with a material/time diagnostic.

## Limitations

Only Source BSP version 20 is supported. The preview is a lightweight polygon renderer, not a complete Source renderer: it does not load textures/materials into the view, implement a depth buffer, or reproduce the game's sky rendering. Sky-material classification depends on texture names stored in the BSP; unusual or missing material metadata may leave some sky faces visible. The ballistic solver ignores collisions, map occlusion, drag, weapon-specific behavior and server-side physics. The heightfield and hillshade are analysis layers only. The trajectory BVH also tests render triangles only; neither it nor the heightfield is a complete Source collision/visibility model. The live tracking path is screen-only and reads the visible `cl_showpos` text. It does not load any in-game module or use a local named pipe.
  
The `cl_showpos` reader is experimental. Click **Select crop with mouse**, then drag a rectangle around the visible `pos:` and `ang:` rows; the coordinates and dimensions are saved. The selection is converted to physical display pixels and targets the primary display. Windowed or borderless game mode is recommended. Font rasterization, game resolution, UI scale and a bright scene behind the text can reduce recognition confidence; invalid or low-confidence samples are rejected. The displayed `pos` is Source's view origin, not a guaranteed muzzle position.

## Build on Windows

Requirements: Visual Studio 2022 with **Desktop development with C++**, CMake 3.21+, and Qt 6.5+ (MSVC 2022 x64 Widgets kit).

Run in Developer PowerShell for VS 2022 from the repository root (adjust the Qt path):

~~~powershell
cd vision-prototype
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\bsp-geometry.exe
~~~

## Quick start

1. Launch `bsp-geometry.exe`.
2. Open **BSP geometry / Геометрия BSP** and choose **Load Source BSP v20… / Загрузить Source BSP v20…**.
3. Leave **Show skybox surfaces / Показывать поверхности скайбокса** unchecked first. Enable it only to inspect those faces.
4. Use **Top-down XY / Сверху XY** to select horizontal coordinates, or **Orbit 3D / 3D-орбита** to rotate with the left mouse button, zoom with the wheel, and double-click to reset the camera.
5. Open **Ballistic lab / Баллистика**, enter/select the target coordinates, then calculate the current arc or solve for the low/high arc. The Z coordinate must be entered manually.
6. For live telemetry, enable `cl_showpos 1`, click **Select crop with mouse** in the Viewer, and select the visible telemetry rows. The external desktop overlay uses only the angles decoded from this screen crop.
7. If needed, adjust X/Y/width/height manually after selection. Inspect confidence and coordinates; use **Use last pos as launch point** only after checking the view-origin/muzzle offset.

For the detailed Russian/English user manual, see `docs/USER_MANUAL.md`.
