# Source BSP Geometry Lab

**A Source Engine BSP geometry viewer and offline ballistic-planning utility.** This repository contains a lightweight browser-based BSP viewer and a native Windows desktop tool built with C++20 and Qt 6.

**Основные возможности / Features**

- Local inspection of Source BSP v20 geometry and lump metadata.
- Top-down XY and rotatable 3D face previews.
- Skybox-material detection with a toggle to show/hide sky-textured faces.
- Russian/English interface with a persistent language setting.
- Offline projectile-trajectory visualization with low/high ballistic-angle solving.
- Optional transparent Windows overlay, drawn by the desktop app itself (no Lua addon): live direction marker driven by calibrated raw mouse movement.
- No OpenCV or computer-vision dependency in the native desktop app.

## Native Windows app

See [the desktop application README](vision-prototype/README.md) for prerequisites, build commands and usage. The app project is named **Source BSP Geometry Lab** and the executable is `bsp-geometry.exe`.

## User manual

- [Bilingual manual (Markdown)](docs/USER_MANUAL.md)

## Browser viewer

Open [index.html](index.html) in a modern browser and select a local `.bsp` file. The browser viewer is kept as a separate lightweight option.

## Live overlay / Оверлей в игре

The optional external overlay is documented in [docs/EXTERNAL_OVERLAY_RU_EN.md](docs/EXTERNAL_OVERLAY_RU_EN.md). It uses Windows Raw Input and draws a transparent, click-through Qt window; it requires no Garry's Mod addon or server changes. It estimates view angles from the initial `getpos` angles plus mouse deltas, so set the game's sensitivity, `m_yaw`, `m_pitch` and FOV correctly. It does not read the game's internal camera state, and camera changes unrelated to mouse input can cause drift.

## Technical scope

The native parser currently supports Source BSP v20. It reconstructs face outlines from BSP geometry and uses material metadata to hide common skybox faces by default. It is not a full Source-engine renderer: textures, lighting, visibility, and engine-specific effects are not recreated. The ballistic lab is an ideal constant-gravity planning aid and does not control a game.
