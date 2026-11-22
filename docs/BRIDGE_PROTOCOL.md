# TFC bridge protocol v1.2

## File bridge

Files live under the local client's `garrysmod/data/tfc_suo/` directory:

- `host.json`: desktop session UUID, protocol, heartbeat and developer options.
- `command.json`: one command envelope with an increasing sequence number.
- `state.json`: current game telemetry and command acknowledgements.

The desktop atomically replaces files using QSaveFile. Lua ignores stale session IDs and repeated sequence numbers. If the desktop heartbeat expires, active aim is cleared.

## Live telemetry

The client Lua script reports:

- `player_position`, `eye_position`, `eye_angles`: player world coordinates and view angles.
- `view_forward`, `player_aim_angles`: world view direction and aim vector angles.
- `view_origin`, `view_attachment`: LVS sight attachment world position when available (falls back to EyePos).
- `vehicle_position`, `vehicle_angles`: LVS platform transform.
- `muzzle`, `muzzle_angle`, `muzzle_attachment`: real world position and angle from the selected muzzle attachment.
- `turret_yaw/pitch`: LVS turret state.
- `gravity`: current live GMod gravity magnitude.
- `turret_ballistics_predicted`, `turret_ballistics_active`, `turret_ballistics_velocity`, `turret_optics_zoom_only`, `zoom_held`, `third_person_mode`, `turret_upright_z`: diagnostic inputs used to determine the active LVS compensation mode.
- `gunner_present`, `gunner_ready`, `ammo_limited`, `ammo_clip`, `ammo_reserve`, `overheated`, `weapon_reason`: selected weapon status.
- `muzzle_error_deg`, `aim_aligned`, `fire_ready`: physical muzzle-vector error and fire gate.
- `server_fire_ack`, `server_fire_message`: response from the server fire relay.

State updates are normally written every 250 ms. TFC uses the platform position as the map marker while seated, and the player's live view direction as its heading arrow. The UI also displays both platform and player coordinates.

## Aim and ballistics

F7 derives the target direction from the selected BSP point and the actual muzzle/sight attachment locations.

Some LVS vehicles provide internal turret ballistics. Their mode may depend on the predictor setting, vehicle upright angle, third-person state, and zoom input. Applying a separate TFC elevation while LVS is already compensating would double-correct the shot.

When `turret_ballistics_active` is true, TFC sends the line-of-sight view angle from `view_origin`, uses the vehicle's `TurretBallisticsProjectileVelocity` and live gravity, and estimates the desired world muzzle angle using LVS's compensation formula. When native LVS compensation is inactive, TFC instead solves the low-angle trajectory with the configured or inferred projectile speed and live/configured gravity. Actual muzzle orientation is verified by comparing world-forward vectors; it is not inferred from the player's camera alone.

The amber trajectory is sampled in world coordinates and drawn over the 3D BSP and top-down views. It is only an approximation: it does not model every BM-30 addon behavior, drag, rocket thrust, fuses or collision. The fallback speed of 4000 Source units/s remains an estimate, not a verified BM-30 specification.

## Fire relay

LVS's server weapon loop asks `ply:lvsKeyDown("ATTACK")`. This is why a client-side hook alone cannot fire the weapon.

The server addon registers `tfc_bridge_fire_request` and `tfc_bridge_fire_ack`. The client sends one request after its local actual-muzzle alignment and readiness gates. The server rechecks LVS seat, selected weapon, heat and finite ammunition, then stores a short-lived per-player pulse. A server-side `LVS.PlayerKeyDown` hook consumes the pulse once and returns `true` for one `ATTACK` read. The standard LVS weapon loop then receives a single attack tick.

LVS `GetMaxAmmo() <= 0` means unlimited reserve. Its `GetClip()` display may also return `-1` for unlimited-ammo weapons, so TFC does not equate `GetClip() <= 0` with empty reserve. Where a weapon has a finite reserve or an explicit clip value, the corresponding readiness check is still applied.

For remote dedicated servers, install the server Lua file on the server itself. TFC's automatic installer can only write to the local GMod directory.

## Command payloads

- `aim`: view `world_yaw/view_pitch` plus expected physical `muzzle_yaw/muzzle_pitch`.
- `fire`: same angle pair, used for a final client-side muzzle alignment check before sending the server net request.
- `cancel`: clears active aim.

## Developer settings

The desktop reads `tfc.cfg` beside the executable:

- `ballistics/projectile_speed=auto`: prefer LVS vehicle velocity; fallback to 4000 units/s. A numeric value is an external-mode override.
- `ballistics/gravity=auto`: use live GMod gravity; a numeric override applies to the external low-arc solver.
- `aim/tolerance_deg`: acceptable actual barrel-direction error.
- `aim/step_deg_per_tick`: player view movement step.
- `game/muzzle_attachment`: exact attachment name or `auto`.
