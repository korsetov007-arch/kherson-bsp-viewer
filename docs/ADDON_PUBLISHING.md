# Publishing the TFC Lua bridge to Steam Workshop

TFC's GMod bridge is a tiny client-side addon. The workshop package contains only:

tfc_bridge/
  lua/
    autorun/
      client/
        tfc_bridge.lua

Do not include tfc.exe, CMake files, BSP maps, build outputs, credentials or other private files.

## Test locally first

1. Copy the folder named `tfc_bridge` into `garrysmod/addons/`.
2. Restart Garry's Mod.
3. Check the console for `[TFC] Lua bridge ready`.
4. Launch TFC and verify the Lua connection and the BM-30 muzzle telemetry.

## Build the .gma on Windows

Find Garry's Mod in Steam → Manage → Browse local files. The tools are in its `bin` folder.

Open Command Prompt and run (adjust the folder paths):

```bat
cd /d "C:\Program Files (x86)\Steam\steamapps\common\GarrysMod\bin"
gmad.exe create -folder "D:\TFC\workshop\tfc_bridge" -out "D:\TFC\workshop\tfc_bridge.gma"
```

Prepare a 512×512 Baseline JPEG icon. Then publish:

```bat
gmpublish.exe create -addon "D:\TFC\workshop\tfc_bridge.gma" -icon "D:\TFC\workshop\tfc_bridge.jpg"
```

Sign in to Steam with the account that should own the addon. After upload, open the new Workshop item from your profile → Workshop Items / My Shared Files, add the description and screenshots, and set visibility to Public when ready.

## Updating later

After changing the Lua file, rebuild the .gma and use:

```bat
gmpublish.exe update -addon "D:\TFC\workshop\tfc_bridge.gma" -id YOUR_WORKSHOP_ITEM_ID -changes "Describe the change"
```

Keep the Workshop addon separate from the desktop EXE release. Users can subscribe to the bridge in Workshop, while TFC itself is distributed as a separate Windows application.
