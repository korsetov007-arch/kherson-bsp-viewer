# TFC — Tactical Fire Control

Windows x64 C++20 / Qt 6 Source BSP viewer and Garry's Mod/LVS fire-control front end.

## Current workflow

- Run `tfc.exe` and open a Source BSP v20 map (drag-and-drop is supported).
- Select a target on the 3D BSP surface or in the top-down X/Y map.
- When GMod is connected, TFC reads live player and vehicle coordinates, view yaw/pitch, the selected LVS weapon, muzzle attachment position/angle, turret state and game gravity.
- A live platform marker and view-direction arrow are drawn on both map views.
- F7 computes a firing direction. If the current LVS vehicle is running its own ballistic compensation, TFC sends a line-of-sight command and predicts the compensated muzzle direction. Otherwise it computes an external low ballistic arc.
- The small × button next to F7 cancels automatic view steering, clears the cached aim solution and returns full manual camera control. While alignment is in progress, TFC refreshes the aim angles from the newest sight/muzzle positions so the preview and controller use the same live geometry. F8 stays gated by weapon readiness and measured muzzle alignment; the UI reports the exact blocking reason.
- An amber trajectory preview is drawn on the 3D map and top-down map.
- F8 requests one server-side LVS `ATTACK` pulse. The server hook returns `true` for exactly one `LVS.PlayerKeyDown("ATTACK")` read.

## Ballistics configuration

The first launch creates `tfc.cfg` beside `tfc.exe`. These developer settings are intentionally not editable in the main UI. Edit the file and restart TFC.

    [ballistics]
    projectile_speed=auto
    gravity=auto

    [aim]
    tolerance_deg=1.5
    step_deg_per_tick=2.0

    [game]
    muzzle_attachment=auto

- `projectile_speed=auto`: use the live LVS vehicle's `TurretBallisticsProjectileVelocity` when present; otherwise fall back to 4000 Source units/s. Set a number to override it. **4000 is only a fallback estimate, not a confirmed BM-30 rocket specification.**
- `gravity=auto`: use live GMod `physenv.GetGravity()`. In native LVS predictor mode, TFC always uses the same live gravity value as LVS. A numeric override applies to TFC's external low-arc mode.
- `tolerance_deg`: maximum difference between desired and actual muzzle world-forward vectors accepted for fire readiness.
- `step_deg_per_tick`: maximum player view-angle movement per GMod `CreateMove` frame.
- `muzzle_attachment=auto`: use LVS `TurretBallisticsMuzzleAttachment`, then try common names (`muzzle`, `muzzle_1`…`muzzle_12`). Set an exact name for a model with a non-standard attachment.

## Why there are two aim modes

Some LVS vehicles already calculate turret ballistics internally. For example, the upstream LVS wheeled-turret ballistics module can apply a gravity compensation only when its predictor is enabled, the vehicle is sufficiently upright, the player is not in third person, and (for `OpticsZoomOnly`) the player is holding zoom. Adding TFC's own ballistic elevation on top of that would double-compensate and send the barrel the wrong way.

TFC detects the vehicle's LVS predictor settings and current zoom/third-person/upright state. When LVS compensation is active, it sends a direct line-of-sight view angle from the vehicle's sight attachment, then predicts the actual muzzle angle using LVS's configured projectile speed and gravity. When the LVS predictor is inactive, TFC computes a low ideal ballistic arc itself. The right-hand panel shows which mode is active. If the mode changes after F7, the old firing solution is invalidated and F7 must be pressed again.

These calculations still approximate the vehicle. The exact BM-30 addon may add rocket-specific motor delay, drag, fuses, or other behavior that is not part of the generic LVS predictor.

## Bridge architecture

The addon contains two Lua files:

- `lua/autorun/client/tfc_bridge.lua`: player/view telemetry, local file bridge and view-angle controller.
- `lua/autorun/server/tfc_bridge_server.lua`: server-side one-shot LVS fire relay.

The desktop and client addon exchange heartbeat/commands/state using GMod's built-in DATA filesystem under `garrysmod/data/tfc_suo/`. For a fire request, the client sends one short GMod net message to the server; it does not open an external socket, inject a DLL or load third-party Lua modules.

**Why a server file is required:** LVS asks `ply:lvsKeyDown("ATTACK")` in the server-side weapon loop. A client-only `LVS.PlayerKeyDown` hook cannot change that server-side result. The new server addon validates the player's LVS seat, selected weapon, finite ammunition and overheat state, then queues a short-lived pulse. The server hook consumes it exactly once. Unlimited-ammo weapons are supported; LVS `GetClip()` may return `-1` when a weapon has no finite ammo limit.

## Setup

1. Build and launch `tfc.exe`.
2. If Steam discovery misses the game, select the `garrysmod` folder containing `addons`.
3. TFC installs both client and server Lua files in `garrysmod/addons/tfc_bridge/`.
4. If GMod was already running during installation, restart it once.
5. Start GMod, enter the BM-30/LVS gunner seat, then launch TFC and wait for CONNECTED.
6. Load the BSP for the same map, select the target, check the live platform marker/heading and amber trajectory, then press F7. F8 becomes available after the actual muzzle direction and weapon-readiness checks pass.

On a remote dedicated server, also install `tfc_bridge/lua/autorun/server/tfc_bridge_server.lua` in the server's own `garrysmod/addons/` directory. The TFC desktop can only install into the local GMod folder.

## Steam Workshop

The Workshop addon folder is `game-addon/tfc_bridge/` and includes both client and server scripts. Do not include `tfc.exe`, CMake files, BSP maps, build outputs, credentials or private files. See [ADDON_PUBLISHING.md](docs/ADDON_PUBLISHING.md).

## Build

Requirements: Visual Studio 2022 Desktop C++, CMake 3.24+, Qt 6.5+ MSVC x64.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure

## Limitations / validation status

- BSP v20 only; the viewer renders BSP geometry but is not a complete Source renderer and does not show textures.
- Target picking intersects the BSP display mesh, not an in-game physics trace. Z/floor selection must be accurate.
- Ballistic preview is an idealized approximation. It is not a complete LVS rocket or BM-30 projectile simulation.
- The actual muzzle/sight attachment names and axes must be verified on the user's BM-30 addon.
- The Lua bridge reports version `1.3`; the desktop connection badge shows the loaded version. Rebuild the Qt executable after bridge source changes so its embedded installer does not restore an older Lua copy.
- Changes in this update have not been compiled with Qt/MSVC or tested in a live GMod session here. Confirm `[TFC] Server-side LVS fire relay ready` in the server console, then test F7 and one F8 pulse at a safe range before relying on it.