# TFC — Tactical Fire Control

Lightweight Windows x64 C++20 / Qt 6 Source BSP viewer and Garry's Mod front end. TFC is a short tactical acronym in the spirit of Combat Mission.

## Prototype
- Single desktop executable: `tfc.exe`.
- Drop a Source BSP v20 file onto `tfc.exe`, or pass it as argv[1]. Opening it without a map shows brief usage instructions.
- The map window stays above Garry's Mod so it remains visible while the game is focused.
- Click a visible surface in the 3D BSP view to select its world-space X/Y/Z point. The top-down tab remains available for X/Y selection.
- F7 aims directly from the live muzzle position at the selected point; no unverified rocket speed or ballistic arc is assumed.
- F8 requests one standard LVS ATTACK pulse, gated by the actual muzzle angle and gunner readiness.
- The Lua addon installs once. Start Garry's Mod first, then TFC in ordinary use.

## Developer bridge config

The first launch creates `tfc.cfg` beside `tfc.exe`. The file is intentionally outside the main UI. Edit it and restart TFC to apply changes.

Default settings:

```ini
[aim]
tolerance_deg=1.5
step_deg_per_tick=2.0

[game]
muzzle_attachment=auto
```

- `tolerance_deg`: maximum actual muzzle-angle error accepted for fire readiness.
- `step_deg_per_tick`: view-angle approach increment per CreateMove frame.
- `muzzle_attachment=auto`: use the LVS vehicle's `TurretBallisticsMuzzleAttachment` property, then try common names such as `muzzle` and `muzzle_1…muzzle_12`. Set an exact attachment name only for a vehicle that needs it.

The addon reports the player's position and view angles, vehicle transform, and `GetAttachment(...).Pos/Ang` from the live in-game muzzle. The actual muzzle angle, not just the camera angle, confirms that the barrel has reached the requested direction. In automatic mode, the bridge tries common muzzle names including `muzzle_1` through `muzzle_12`.

## Bridge

The bridge uses Garry's Mod's built-in DATA filesystem; no open network port, DLL, socket library or external Lua module. Session UUIDs and sequence numbers prevent stale commands being replayed. See [BRIDGE_PROTOCOL.md](docs/BRIDGE_PROTOCOL.md).

## First-time setup

1. Build and run `tfc.exe`.
2. If Steam discovery misses the game, choose the `garrysmod` folder containing `addons`.
3. TFC installs `addons/tfc_bridge/lua/autorun/client/tfc_bridge.lua` automatically. If Windows blocks the write, it offers a one-time UAC elevation for this file copy only.
4. If the game was already running during installation, restart it once.
5. Later: start GMod, enter the LVS vehicle, then launch TFC and wait for CONNECTED.

## Steam Workshop

Yes: the bridge can be published as a separate Workshop addon. Its folder is `game-addon/tfc_bridge/` in the source/package layout; it contains only the client Lua script. See [ADDON_PUBLISHING.md](docs/ADDON_PUBLISHING.md) for testing, gmad/gmpublish commands and the icon requirements. The desktop EXE is not part of the Workshop addon.

## Build

Requirements: Visual Studio 2022 Desktop C++, CMake 3.24+, Qt 6.5+ MSVC x64.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Known limitations

BSP v20 only; the viewer renders BSP geometry without textures or a full Source renderer. Target picking intersects the visible BSP mesh and is not an in-game collision trace. The bridge changes view angles through Garry's Mod's `CreateMove` hook and does not inject DLLs or use an external network connection. Weapon behavior and muzzle layout vary between addons; test F7/F8 in the actual BM-30 setup. The bridge can confirm its command and readiness checks, not that a projectile launched or hit. No live game test is claimed.
