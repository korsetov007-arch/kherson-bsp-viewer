# TFC — Tactical Fire Control

Lightweight Windows x64 C++20 / Qt 6 Source BSP viewer and Garry's Mod front end. TFC is a short tactical acronym in the spirit of Combat Mission.

## Prototype
- Single desktop executable: `tfc.exe`.
- Drop a Source BSP v20 file onto the app, or pass it as argv[1].
- Reuses the earlier BSP v20 parser and 3D view.
- Top-down tab selects X/Y; set Z manually because maps can have multiple floors.
- F7 computes a low ideal ballistic arc and sends a view-angle request.
- F8 requests one standard LVS ATTACK pulse, gated by the actual muzzle angle and gunner readiness.
- The Lua addon installs once. Start Garry's Mod first, then TFC in ordinary use.

## Developer ballistics config

The first launch creates `tfc.cfg` beside `tfc.exe`. The file is intentionally outside the main UI. Edit it and restart TFC to apply changes.

Default profile:

```ini
[ballistics]
projectile_speed=4000
gravity=600

[aim]
tolerance_deg=1.5
step_deg_per_tick=2.0

[game]
muzzle_attachment=auto
```

- `projectile_speed`: projectile speed in Source units per second (default 4000).
- `gravity`: gravity magnitude used by the ballistic solver (default 600).
- `tolerance_deg`: maximum actual muzzle-angle error accepted for fire readiness.
- `step_deg_per_tick`: view-angle approach increment per CreateMove frame.
- `muzzle_attachment=auto`: use the LVS vehicle's `TurretBallisticsMuzzleAttachment` property, then fall back to `muzzle`. Set an exact attachment name only for a vehicle that needs it.

The addon reports `GetAttachment(...).Ang` from the actual in-game muzzle attachment. That world yaw/pitch, not the player's camera angle, is used to confirm that the barrel has reached the requested direction. The muzzle position from the same attachment is the ballistic launch origin.

## Bridge

The bridge uses Garry's Mod's built-in DATA filesystem; no open network port, DLL, socket library or external Lua module. Session UUIDs and sequence numbers prevent stale commands being replayed. See [BRIDGE_PROTOCOL.md](docs/BRIDGE_PROTOCOL.md).

## First-time setup

1. Build and run `tfc.exe`.
2. If Steam discovery misses the game, choose the `garrysmod` folder containing `addons`.
3. TFC installs `addons/tfc_bridge/lua/autorun/client/tfc_bridge.lua` automatically.
4. If the game was already running during installation, restart it once.
5. Later: start GMod, enter the LVS vehicle, then launch TFC and wait for CONNECTED.

## Steam Workshop

Yes: the bridge can be published as a separate Workshop addon. Its folder is `game-addon/tfc_bridge/` in the source/package layout; it contains only the client Lua script. See [ADDON_PUBLISHING.md](docs/ADDON_PUBLISHING.md) for testing, gmad/gmpublish commands and the icon requirements. The desktop EXE is not part of the Workshop addon.

## Build

Requirements: Visual Studio 2022 Desktop C++, CMake 3.24+, Qt 6.5+ MSVC x64.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Known limitations

BSP v20 only; no full Source renderer or exact collision. Z selection is manual. Ballistics assume constant gravity and projectile speed, without drag, terrain collision or weapon-specific simulation. 4000 units/s is still an unverified estimate. The exact BM-30 addon must be tested for muzzle attachment, turret response and attack action. The bridge confirms a sent pulse, not an actual projectile launch. No build or live game test is claimed yet.
