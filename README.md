# Source BSP Geometry Lab

**A Source Engine BSP geometry viewer and offline ballistic-planning utility.** This repository contains a lightweight browser-based BSP viewer and a native Windows desktop tool built with C++20 and Qt 6.

**Основные возможности / Features**

- Local inspection of Source BSP v20 geometry and lump metadata.
- Top-down XY and rotatable 3D face previews.
- Skybox-material detection with a toggle to show/hide sky-textured faces.
- Russian/English interface with a persistent language setting.
- Offline projectile-trajectory visualization with low/high ballistic-angle solving.
- Optional local Garry's Mod client addon: camera/player telemetry, target-angle HUD marker, and no server-side addon requirement.
- No OpenCV or computer-vision dependency in the native desktop app.

## Native Windows app

See [the desktop application README](vision-prototype/README.md) for prerequisites, build commands and usage. The app project is named **Source BSP Geometry Lab** and the executable is `bsp-geometry.exe`.

## User manual

- [Bilingual manual (Markdown)](docs/USER_MANUAL.md)

## Browser viewer

Open [index.html](index.html) in a modern browser and select a local `.bsp` file. The browser viewer is kept as a separate lightweight option.

## Garry's Mod live link

An optional clientside addon and bilingual install guide are in [gmod-addon](gmod-addon/README.md). It exchanges local JSON files in `garrysmod/data/source_bsp_geometry_lab/`; both the desktop app and game must run on the same Windows account, and the loaded BSP filename must match the active game map. The HUD marker is only a visual indicator; it does not control input or fire.

## Technical scope

The native parser currently supports Source BSP v20. It reconstructs face outlines from BSP geometry and uses material metadata to hide common skybox faces by default. It is not a full Source-engine renderer: textures, lighting, visibility, and engine-specific effects are not recreated. The ballistic lab is an ideal constant-gravity planning aid and does not control a game.
