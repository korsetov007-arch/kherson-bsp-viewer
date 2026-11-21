# SBGL Native Bridge (Windows)

This is the native client module + thin Lua adapter for Source BSP Geometry Lab.

## Architecture

- Qt app listens on the local Windows named pipe `SBGLBridge`.
- The GMod client binary module `gmcl_sbgl_bridge_win64.dll` connects to that pipe.
- Lua uses `require("sbgl_bridge")`, sends telemetry JSON and polls target-marker commands.
- The Lua HUD draws the optional marker inside GMod. It does not modify movement, aim input, or firing.

This is a **native Lua binary module**, not arbitrary DLL injection. GMod's module loader loads it through its documented `require` API.

## One-click install

From the repository, double-click `gmod-addon/install-sbgl-bridge.bat`. It attempts to find the Steam installation, builds the x64 module with CMake, then copies the DLL and Lua adapter into the Garry's Mod folders. Requirements: Visual Studio 2022 with Desktop development with C++, CMake, Git, and a 64-bit Windows Garry's Mod client.

If the game is in a custom location, run PowerShell:

```powershell
.\gmod-addon\install-sbgl-bridge.ps1 -GamePath "D:\SteamLibrary\steamapps\common\GarrysMod"
```

The manual build path is also available from this folder:

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

## Compatibility and test status

The Qt app now listens on the named pipe `SBGLBridge`, receives telemetry JSON, and sends target-marker commands back to the Lua adapter. The module source is not a prebuilt/release-verified DLL: the installer builds it locally. This repository change has not been compiled or verified in-game yet.

A server/client configuration that blocks local client Lua or binary modules prevents the adapter from loading; this bridge does not bypass that restriction. LVS vehicle/weapon APIs are best-effort and need testing with each vehicle addon.
