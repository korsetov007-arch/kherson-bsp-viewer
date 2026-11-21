# SBGL Garry's Mod integration

The client-side Lua adapter is in `source_bsp_geometry_lab/lua/autorun/client/cl_sbgl_live.lua`. The native binary module and build instructions are in [native-bridge/README.md](native-bridge/README.md).

**Important:** this integration is not an arbitrary DLL injector. It uses the documented GMod binary Lua module loader (`require`) and the Qt app's local Windows named pipe. Local client Lua/binary-module loading must be permitted by the client/server configuration; it cannot override a server that disables it.

The source is committed, but the binary DLL has not been built or tested from this repository. Build it for the exact Garry's Mod client architecture and verify the LVS API on the specific vehicle/addon.
