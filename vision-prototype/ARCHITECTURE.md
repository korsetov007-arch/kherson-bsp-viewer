# Source BSP Geometry Lab — architecture

## Application structure

- **Qt 6 / C++20 desktop UI** in `src/main.cpp`, including persistent English/Russian language settings and a sky-surface visibility toggle.
- **Language helper** in `include/app_language.hpp`; the active language is shared by the main window, parser and visualizations.
- **BSP parser** in `src/bsp_file.cpp`; validates Source BSP v20 headers and lump ranges, extracts vertices/edges/surfedges/faces, and resolves face material names through texture-info, texture-data and string-table lumps.
- **Skybox handling:** face material names matching common Source sky patterns are marked as sky surfaces and excluded from geometry views by default. The UI can enable them for inspection.
- **Top-down map view** in `src/bsp_map_view.cpp`; renders XY polygons, uses visible geometry for bounds, and reports clicked XY coordinates.
- **3D map preview** in `src/bsp_3d_view.cpp`; Z-up perspective camera with an orthonormal view basis, far-to-near depth sorting, drag-to-orbit, wheel zoom and double-click reset. Camera orientation is preserved when a visibility toggle leaves map bounds unchanged.
- **Ballistic view** in `src/ballistics_view.cpp`; draws trajectory in XY, XZ and oblique 3D projections, with the visible BSP geometry overlaid.
- **Parser tests** in `tests/bsp_file_tests.cpp`; include BSP header/geometry validation, texture metadata resolution, sky face filtering, and invalid-input handling.
- **Screen-only telemetry** in `src/cl_showpos_reader.cpp`; DXGI Desktop Duplication captures a manually selected crop from the primary monitor. A translucent full-screen selector lets the user drag a rectangle around visible `pos:`/`ang:` rows; coordinates are converted to physical display pixels and saved. The pixel-template matcher and numeric/range validation produce a confidence-scored sample.
- **Heightfield** in `src/heightfield.cpp`; rasterizes upward-facing BSP/displacement triangles into minimum-ground and maximum-canopy layers, generates a hillshade, and caches 128/256-unit contour lines with marching squares. The 2D view can display the hillshade on demand.

## Ballistic model

The UI accepts start/target XYZ coordinates, speed, gravity, yaw, elevation and flight time. The solver computes low/high ideal elevation angles under constant gravity where a solution exists. Calculations remain local; the preview is not connected to game input.

## Known limitations

- Only BSP version 20 is supported.
- This is not a full Source engine renderer: it does not render material textures, use a GPU z-buffer, or reproduce engine skybox rendering.
- Sky-surface classification relies on texture names encoded in the BSP; incomplete or unusual metadata may prevent a match.
- The ballistic model does not simulate collision, map occlusion, drag, splash radius, weapon-specific behavior or engine/server physics.
- Heightfield cells are point samples at finite resolution and only currently rasterize upward-facing surfaces; they are not a substitute for Source brush/prop collision or an exact line trace.
- Screen telemetry requires `cl_showpos` to be visibly rendered on the primary display. Windowed/borderless mode is recommended because exclusive-fullscreen games may cover the selector. Matching depends on the rendered font; low-confidence samples are rejected. `pos` is the view origin, not necessarily the weapon muzzle.
