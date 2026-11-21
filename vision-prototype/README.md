# Source BSP Geometry Lab

Bilingual Windows desktop tool for inspecting Source Engine BSP v20 geometry and planning ideal projectile trajectories. Built with C++20 and Qt 6; it does not require OpenCV or any computer-vision model.

## Features

- Load a Source BSP v20 map and inspect its lump table.
- View polygon geometry from above (XY) or in a rotatable 3D projection.
- Detect common Source skybox material names from BSP texture metadata. Sky-textured faces are hidden by default so giant sky planes are less likely to overwhelm the level preview; use **Show skybox surfaces** to inspect them.
- Switch the interface between Russian and English via **Settings → Language**. The selected language is saved for the next launch.
- Select X/Y coordinates by clicking the map. Enter Z manually to choose the correct floor.
- Plan a projectile path in XY, XZ and oblique 3D views, and solve for low or high ideal ballistic arcs.
- Run synthetic BSP parser tests with CTest.
- Optional transparent external overlay drawn by the desktop app. It uses Windows Raw Input to estimate yaw/pitch from mouse deltas; no Garry's Mod addon or server-side access is needed. See [the bilingual overlay guide](../docs/EXTERNAL_OVERLAY_RU_EN.md).

## Limitations

Only Source BSP version 20 is supported. The preview is a lightweight polygon renderer, not a complete Source renderer: it does not load textures/materials into the view, implement a depth buffer, or reproduce the game's sky rendering. Sky-material classification depends on texture names stored in the BSP; unusual or missing material metadata may leave some sky faces visible. The ballistic solver ignores collisions, map occlusion, drag, weapon-specific behavior and server-side physics. The external overlay estimates angles from raw mouse deltas and user-entered starting angles; it is not internal game telemetry and can drift if controls, cameras, sensitivity or FOV change.

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
4. Use **Top-down XY / Сверху XY** to select horizontal coordinates, or **Orbit 3D / 3D-орбита** to rotate with the left mouse button and zoom with the wheel.
5. Open **Ballistic lab / Баллистика**, enter/select the target coordinates, then calculate the current arc or solve for the low/high arc. The Z coordinate must be entered manually.
6. For the optional external overlay, use **Paste getpos from clipboard** to fill launch coordinates and initial yaw/pitch, select a target and solve an arc, adjust sensitivity/`m_yaw`/`m_pitch` and horizontal FOV, then click **Start overlay**. Switch back to the game. Use **Stop overlay** to close it. Details: [overlay guide](../docs/EXTERNAL_OVERLAY_RU_EN.md).

For the detailed Russian/English user manual, see `docs/USER_MANUAL.md`.
