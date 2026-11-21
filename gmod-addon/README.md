# SBGL client telemetry bridge

This is an optional **client-side** Garry's Mod addon for the Source BSP Geometry Lab Qt app.

## Install once

Copy the `source_bsp_geometry_lab` folder into:

`<GarrysMod>/garrysmod/addons/`

Expected file:
`garrysmod/addons/source_bsp_geometry_lab/lua/autorun/client/cl_sbgl_live.lua`

Restart Garry's Mod to load it. The addon does not install anything on the server and does not need server admin permissions.

## While the game is running

GMod does not expose a general, reliable external API for injecting arbitrary Lua into an already-running client. If the client console permits `lua_openscript_cl`, the file may be manually loaded with that command, but command availability depends on the game/client configuration. The reliable default is to install once and restart the game.

## Output

The addon updates this local file approximately 10 times per second:

`garrysmod/data/source_bsp_geometry_lab/telemetry.json`

It includes player position, render-context eye position/angles/vector, FOV, and best-effort LVS vehicle/weapon-handler class and aim vector. The Qt app must read this file to consume the data; merely installing this addon does not itself connect the app.

## Compatibility notes

- LVS APIs vary by vehicle/addon version. Missing methods are reported as unavailable rather than guessed.
- `EyePos()` and `EyeAngles()` are camera/player context data, not guaranteed to be the rocket launcher barrel direction.
- The weapon handler's `GetAimVector()` is exported only when that method exists and returns successfully.
- The small HUD label is diagnostic only; it does not modify controls or fire weapons.
