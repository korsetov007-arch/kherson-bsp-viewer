# Publishing the TFC bridge to Steam Workshop

The addon package contains both client and server autorun scripts:

    tfc_bridge/
      lua/
        autorun/
          client/
            tfc_bridge.lua
          server/
            tfc_bridge_server.lua

Do not include `tfc.exe`, CMake files, BSP maps, build outputs, credentials or private files.

## Test locally first

1. Copy the folder named `tfc_bridge` into `garrysmod/addons/`.
2. Restart Garry's Mod.
3. Check the client console for `[TFC] Lua bridge ready`.
4. Check the server console for `[TFC] Server-side LVS fire relay ready`.
5. Launch TFC and verify the live position, view heading and muzzle telemetry. Test F7 and a single F8 request at a safe range.

For a remote dedicated server, its server folder must also contain `lua/autorun/server/tfc_bridge_server.lua`.

## Build the .gma on Windows

Find Garry's Mod in Steam → Manage → Browse local files. The tools are in its `bin` folder.

    cd /d "C:\\Program Files (x86)\\Steam\\steamapps\\common\\GarrysMod\\bin"
    gmad.exe create -folder "D:\\TFC\\workshop\\tfc_bridge" -out "D:\\TFC\\workshop\\tfc_bridge.gma"

Prepare a 512×512 Baseline JPEG icon, then publish:

    gmpublish.exe create -addon "D:\\TFC\\workshop\\tfc_bridge.gma" -icon "D:\\TFC\\workshop\\tfc_bridge.jpg"

Sign in to Steam with the account that should own the addon. After upload, open the new Workshop item from your profile → Workshop Items / My Shared Files, add description and screenshots, then set visibility to Public when ready.

## Updating

After changing Lua files, rebuild the .gma and run:

    gmpublish.exe update -addon "D:\\TFC\\workshop\\tfc_bridge.gma" -id YOUR_WORKSHOP_ITEM_ID -changes "Describe the change"

Keep the Workshop addon separate from the desktop EXE release.
