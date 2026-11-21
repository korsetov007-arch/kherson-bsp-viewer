# Source BSP Geometry Lab

**A Source Engine BSP geometry viewer and offline ballistic-planning utility.** This repository contains a lightweight browser-based BSP viewer and a native Windows desktop tool built with C++20 and Qt 6.

**Основные возможности / Features**

- Local inspection of Source BSP v20 geometry and lump metadata.
- Top-down XY and rotatable 3D face previews, with basic Source displacement surface reconstruction.
- Skybox-material detection with a toggle to show/hide sky-textured faces.
- Russian/English interface with a persistent language setting.
- Offline projectile-trajectory visualization with low/high ballistic-angle solving.
- Screen-only `cl_showpos` telemetry using DXGI Desktop Duplication and classic pixel-template matching; select the crop by dragging a rectangle over the game screen.
- On-demand two-layer terrain heightfield (`ground`/`canopy`), hillshade, and cached 128/256-unit contour lines for the 2D map.
- Median-split triangle BVH for approximate trajectory/surface intersection checks against BSP render triangles.
- No OpenCV, OCR engine or neural-network model is required for the native desktop app.

## Native Windows app

See [the desktop application README](vision-prototype/README.md) for prerequisites, build commands and usage. The app project is named **Source BSP Geometry Lab** and the executable is `bsp-geometry.exe`.

## User manual

- [Bilingual manual (Markdown)](docs/USER_MANUAL.md)

## Browser viewer

Open [index.html](index.html) in a modern browser and select a local `.bsp` file. The browser viewer is kept as a separate lightweight option.

## Live overlay / Оверлей в игре

The telemetry path is exclusively screen-based: enable `cl_showpos 1` in Garry's Mod, launch the desktop Viewer, click **Select crop with mouse**, and drag a rectangle around the visible `pos:` and `ang:` lines (include `vel:` if visible). The crop is saved between launches. Windowed or borderless mode is recommended; the selector uses the primary monitor. No Lua addon, binary game module or named pipe is required.

## Technical scope

The native parser currently supports Source BSP v20, reconstructs face outlines and basic displacement triangles from DISPINFO/DISP_VERTS/DISP_TRIS, and uses material metadata to hide common skybox faces by default. The optional heightfield rasterizes upward-facing triangles into minimum-ground and maximum-canopy layers and generates a hillshade on demand. This is not yet a full Source-engine renderer or collision system: materials/textures, complete prop collision, accurate brush/portal semantics, full trace parity, prop collision, and exact brush-solid collision are not implemented. The trajectory BVH is only a render-surface approximation. The ballistic lab is an ideal constant-gravity planning aid and does not control a game. The `cl_showpos` fallback is experimental, captures a crop from the primary display, and has not yet been validated with an actual GMod capture; the displayed view origin is not guaranteed to be the muzzle position.
