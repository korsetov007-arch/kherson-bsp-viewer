# TFC bridge addon

This is the Workshop-ready Garry's Mod addon. It contains both Lua realms:

- `lua/autorun/client/tfc_bridge.lua` — live telemetry, file bridge, LVS-mode detection and view-angle control.
- `lua/autorun/server/tfc_bridge_server.lua` — server-side one-shot LVS ATTACK relay.

For local/single-player/listen-server use, copy the folder named `tfc_bridge` into `garrysmod/addons/` and restart GMod. TFC normally installs both files automatically on first run.

For a remote dedicated server, install the server file in the server's own `garrysmod/addons/tfc_bridge/lua/autorun/server/` directory. The client telemetry file remains installed on each player client.

Publishing instructions: see [../docs/ADDON_PUBLISHING.md](../docs/ADDON_PUBLISHING.md).
