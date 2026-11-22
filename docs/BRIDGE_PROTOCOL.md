# TFC file bridge protocol v1.1

Files live under `garrysmod/data/tfc_suo/`:
- `host.json`: desktop session UUID, protocol, heartbeat and developer options.
- `command.json`: one command envelope with an increasing sequence number.
- `state.json`: game telemetry and acknowledgement.

The desktop atomically replaces command files using QSaveFile. Lua ignores stale session IDs and repeated sequence numbers. If the desktop heartbeat expires, active aim and pending fire are cleared.

## Telemetry and angles

Lua reads both position and angle from the configured LVS muzzle attachment's `GetAttachment(index)` result:
- `muzzle.x/y/z`: actual in-game attachment position, used as the projectile origin.
- `muzzle_angle.yaw/pitch/roll`: actual world-space attachment angle from the game.
- `turret_yaw/pitch`: LVS turret state for diagnostics.

TFC uses the actual muzzle position to solve the path and the actual muzzle angle to determine whether the barrel has reached the requested direction. Camera/player angles are only used as the input that lets the standard LVS turret controller move; they are not treated as proof of barrel alignment.

## Commands

- `aim`: desired world yaw and Source pitch. The addon changes player view angles through CreateMove; LVS then drives its turret normally.
- `fire`: a single temporary `LVS.PlayerKeyDown` override for the standard LVS `ATTACK` action, gated on physical muzzle-angle tolerance, gunner presence, clip availability and no overheat.
- `cancel`: clear active aim and pending fire.

## Developer settings

The desktop reads `tfc.cfg` next to its executable. Settings are sent to Lua in `host.json`: muzzle attachment override, physical barrel-angle tolerance and view approach step. The ballistic solver uses projectile speed and gravity from that file. Defaults are 4000 units/s and gravity 600.

The exact BM-30 addon must still be tested in-game. The speed is a planning baseline, not a verified shell speed, and a custom addon may use a non-standard muzzle attachment or weapon input.
