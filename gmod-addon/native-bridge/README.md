# SBGL Native Bridge (Windows)

This is the native client module + thin Lua adapter for Source BSP Geometry Lab.

## Architecture

- Qt app listens on the local Windows named pipe `SBGLBridge`.
- The GMod client binary module `gmcl_sbgl_bridge_win64.dll` connects to that pipe.
- Lua uses `require("sbgl_bridge")`, sends telemetry JSON and polls target-marker commands.
- The Lua HUD draws the optional marker inside GMod. It does not modify movement, aim input, or firing.

This is a **native Lua binary module**, not arbitrary DLL injection. GMod's module loader loads it through its documented `require` API.

## Build the native module

Requirements: Visual Studio 2022 with Desktop development with C++, CMake, Git, and a 64-bit Windows Garry's Mod client.

From this folder:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The Facepunch module-base headers are fetched by CMake. Copy the resulting `gmcl_sbgl_bridge_win64.dll` into:

`<Garry's Mod>/garrysmod/lua/bin/`

Binary modules must be installed in `lua/bin`; they cannot be bundled as an ordinary addon. See the Facepunch binary-module documentation.

## Install the Lua adapter

Copy `../source_bsp_geometry_lab/lua/autorun/client/cl_sbgl_live.lua` to:

`<Garry's Mod>/garrysmod/addons/source_bsp_geometry_lab/lua/autorun/client/`

Restart GMod. The adapter will attempt to load the native module and reconnect when the Qt app starts.

## Important status

This is source code, not a prebuilt/release-verified DLL. Build and test it for the exact client architecture. The Qt application must listen on the named pipe and implement the JSON protocol; do not consider the end-to-end bridge finished until that integration is present and tested in-game.
