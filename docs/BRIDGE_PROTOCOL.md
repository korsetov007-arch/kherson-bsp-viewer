# TFC file bridge protocol v1

Files live under garrysmod/data/tfc_suo/:
- host.json: desktop session UUID, protocol and heartbeat.
- command.json: one command envelope with increasing sequence number.
- state.json: telemetry and command acknowledgement.

The desktop atomically replaces command files using QSaveFile. Lua ignores stale session IDs and repeated sequence numbers. If the desktop heartbeat expires, active aim and pending fire are cleared.

Commands:
- aim: world yaw and Source view pitch. The addon updates player view angles through CreateMove; it does not directly overwrite LVS turret state.
- fire: a single temporary LVS.PlayerKeyDown override for the standard LVS ATTACK action, gated on current view alignment, gunner presence, clip availability and no overheat.
- cancel: clear the active aim.

Integration caveats:
The exact BM-30 addon must be tested in-game. The fallback speed of 4000 units/s comes from an earlier HUD predictor and is not a verified shell speed. Attachment name and turret behavior may vary by vehicle. View alignment is not proof that the physical turret has aligned. The bridge reports a sent attack pulse, not a confirmed projectile launch.
