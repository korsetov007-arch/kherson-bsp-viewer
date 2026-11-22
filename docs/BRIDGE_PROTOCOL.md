# TFC file bridge protocol v1.1

Files live under `garrysmod/data/tfc_suo/`:
- `host.json`: desktop session UUID, protocol, heartbeat and developer options.
- `command.json`: one command envelope with an increasing sequence number.
- `state.json`: game telemetry and acknowledgement.

The desktop atomically replaces command files using QSaveFile. Lua ignores stale session IDs and repeated sequence numbers. If the desktop heartbeat expires, active aim and pending fire are cleared.

## Telemetry and angles

Lua reads the local player's `GetPos()`, `EyePos()`, and `EyeAngles()`, the LVS vehicle's transform, and the muzzle attachment's `GetAttachment(index)` result:
- `muzzle.x/y/z`: actual in-game attachment position, used as the projectile origin.
- `muzzle_angle.yaw/pitch/roll`: actual world-space attachment angle from the game.
- `player_position`, `eye_position`, `eye_angles`: player position and camera state.
- `vehicle_position`, `vehicle_angles`: vehicle transform.
- `turret_yaw/pitch`: LVS turret state for diagnostics.

TFC aims directly from the actual muzzle position to the selected BSP point. The actual muzzle angle determines whether the barrel has reached the requested direction. Camera/player angles are only the input to the standard LVS turret controller; they are not treated as proof of barrel alignment. Automatic muzzle lookup tries the vehicle's `TurretBallisticsMuzzleAttachment` setting, then common names (`muzzle`, `muzzle_2`, `muzzle_1` through `muzzle_12`).

## Commands

- `aim`: desired world yaw and Source pitch for direct line-of-sight to the selected point. The addon changes player view angles through CreateMove; LVS then drives its turret normally.
- `fire`: a single temporary `LVS.PlayerKeyDown` override for the standard LVS `ATTACK` action, gated on physical muzzle-angle tolerance, gunner presence, clip availability and no overheat.
- `cancel`: clear active aim and pending fire.

## Developer settings

The desktop reads `tfc.cfg` next to its executable. Settings are sent to Lua in `host.json`: muzzle attachment override, physical barrel-angle tolerance and view approach step.

Different BM-30 addons can use different rocket behavior, muzzle attachments, and weapon inputs. The bridge does not assume a projectile speed or simulate a ballistic path. Test F7/F8 in the actual game setup; readiness feedback does not guarantee launch or impact.
