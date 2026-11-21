# Source BSP Geometry Lab

**A Source Engine BSP geometry viewer and offline ballistic-planning utility.** This repository contains a lightweight browser-based BSP viewer and a native Windows desktop tool built with C++20 and Qt 6.

**Основные возможности / Features**

- Local inspection of Source BSP v20 geometry and lump metadata.
- Top-down XY and rotatable 3D face previews.
- Skybox-material detection with a toggle to show/hide sky-textured faces.
- Russian/English interface with a persistent language setting.
- Offline projectile-trajectory visualization with low/high ballistic-angle solving.
- Optional live bridge for Garry's Mod: Qt local named pipe, native Lua binary module, LVS telemetry adapter and in-game HUD marker (requires clientside Lua/module loading to be permitted).
- No OpenCV or computer-vision dependency in the native desktop app.

## Native Windows app

See [the desktop application README](vision-prototype/README.md) for prerequisites, build commands and usage. The app project is named **Source BSP Geometry Lab** and the executable is `bsp-geometry.exe`.

## User manual

- [Bilingual manual (Markdown)](docs/USER_MANUAL.md)

## Browser viewer

Open [index.html](index.html) in a modern browser and select a local `.bsp` file. The browser viewer is kept as a separate lightweight option.

## Live overlay / Оверлей в игре

The native bridge source and setup are documented in [gmod-addon/native-bridge/README.md](gmod-addon/native-bridge/README.md). The app listens on a local Windows named pipe; the GMod binary module sends telemetry and receives HUD target-marker commands. This is a documented GMod native Lua module, not arbitrary DLL injection. A compatible client must allow local client Lua and binary modules; many public servers disable this, so the bridge cannot force itself to load there. The native module and end-to-end build still need local compilation and in-game verification.

## Technical scope

The native parser currently supports Source BSP v20. It reconstructs face outlines from BSP geometry and uses material metadata to hide common skybox faces by default. It is not a full Source-engine renderer: textures, lighting, visibility, and engine-specific effects are not recreated. The ballistic lab is an ideal constant-gravity planning aid and does not control a game.
