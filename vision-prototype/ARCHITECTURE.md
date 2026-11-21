# BSP Geometry & Ballistics — architecture

## Application structure

- **Qt 6 / C++20 desktop shell** in src/main.cpp; exposes BSP geometry and ballistic planning tabs.
- **BSP parser** in src/bsp_file.cpp; validates the Source BSP v20 header and lump ranges, then extracts geometry records.
- **Top-down map view** in src/bsp_map_view.cpp; draws face outlines in XY and reports the XY coordinate under the cursor.
- **3D map preview** in src/bsp_3d_view.cpp; provides drag-to-rotate and wheel-to-zoom interaction using a lightweight painter-based projection.
- **Ballistic view** in src/ballistics_view.cpp; displays trajectory samples in XY, XZ, and oblique 3D projections.
- **Parser tests** in tests/bsp_file_tests.cpp; exercise header parsing, geometry reconstruction, and invalid-input handling.

## Ballistic model

The UI accepts launch and target coordinates plus projectile speed, gravity, yaw, pitch/elevation, and flight time. The target solver computes low/high ideal elevation angles under constant gravity when a solution exists. The trajectory preview is calculated locally and is not connected to game input.

The model does not include collisions, map occlusion, air drag, splash radius, weapon-specific effects, or engine/server physics. The BSP top-down view provides only X/Y, so Z must be chosen explicitly when multiple floors or ceilings may share the same horizontal location.

## Validation and limitations

- BSP version 20 is the only supported format in this prototype.
- Materials and textures are not rendered; surfaces are represented by face outlines.
- The 3D preview is not a z-buffer renderer, so intersecting or back-facing geometry may overlap imperfectly.
- Synthetic tests cover parser validation and geometry reconstruction. Runtime behavior should still be checked with representative BSP files and known coordinate points.
