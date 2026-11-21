# SBGL Garry's Mod integration

Run [install-sbgl-bridge.bat](install-sbgl-bridge.bat) for a one-click Windows setup. It finds the game, builds the native module and installs both the DLL and the client Lua adapter. The adapter source is in `source_bsp_geometry_lab/lua/autorun/client/cl_sbgl_live.lua`; manual build instructions are in [native-bridge/README.md](native-bridge/README.md).

**Important:** this integration is not an arbitrary DLL injector. It uses the documented GMod binary Lua module loader (`require`) and the Qt app's local Windows named pipe. Local client Lua/binary-module loading must be permitted by the client/server configuration; it cannot override a server that disables it.

The source is committed, but the binary DLL has not been built or tested from this repository. Build it for the exact Garry's Mod client architecture and verify the LVS API on the specific vehicle/addon.
