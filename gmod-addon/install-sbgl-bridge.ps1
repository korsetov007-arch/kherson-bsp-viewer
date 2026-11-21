param(
    [string]$GamePath = ""
)
$ErrorActionPreference = "Stop"

function Find-GModPath {
    $candidates = @()
    if ($GamePath) { $candidates += $GamePath }
    $candidates += @(
        "\${env:ProgramFiles(x86)}\Steam\steamapps\common\GarrysMod",
        "$env:ProgramFiles\Steam\steamapps\common\GarrysMod",
        "\${env:ProgramFiles(x86)}\Steam\steamapps\common\Garry's Mod",
        "$env:ProgramFiles\Steam\steamapps\common\Garry's Mod"
    )
    $steamRoots = @()
    $steamRoots += "\${env:ProgramFiles(x86)}\Steam"
    $steamRoots += "$env:ProgramFiles\Steam"
    foreach ($root in $steamRoots) {
        $vdf = Join-Path $root "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            $text = Get-Content $vdf -Raw
            foreach ($match in [regex]::Matches($text, '"path"\s+"([^"]+)"')) {
                $library = $match.Groups[1].Value.Replace('\\','\')
                $candidates += (Join-Path $library "steamapps\common\GarrysMod")
                $candidates += (Join-Path $library "steamapps\common\Garry's Mod")
            }
        }
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if ($candidate -and (Test-Path (Join-Path $candidate "garrysmod"))) {
            return (Resolve-Path $candidate).Path
        }
    }
    return $null
}

$nativeDir = Join-Path $PSScriptRoot "native-bridge"
$game = Find-GModPath
if (-not $game) {
    throw "Could not find Garry's Mod. Re-run with -GamePath 'D:\SteamLibrary\steamapps\common\GarrysMod'."
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found in PATH. Install CMake and the Visual Studio 2022 C++ desktop workload first."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git was not found in PATH. Install Git for Windows first."
}

Write-Host "Garry's Mod: $game"
Write-Host "Configuring native SBGL Bridge..."
cmake -S $nativeDir -B (Join-Path $nativeDir "build") -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
cmake --build (Join-Path $nativeDir "build") --config Release
if ($LASTEXITCODE -ne 0) { throw "Native bridge build failed." }

$dll = Get-ChildItem (Join-Path $nativeDir "build") -Recurse -File -Filter "gmcl_sbgl_bridge_win64.dll" | Select-Object -First 1
if (-not $dll) {
    throw "Build completed but gmcl_sbgl_bridge_win64.dll was not found. Inspect native-bridge/build output."
}
$binDir = Join-Path $game "garrysmod\lua\bin"
$luaDir = Join-Path $game "garrysmod\addons\source_bsp_geometry_lab\lua\autorun\client"
New-Item -ItemType Directory -Force -Path $binDir | Out-Null
New-Item -ItemType Directory -Force -Path $luaDir | Out-Null
Copy-Item $dll.FullName (Join-Path $binDir "gmcl_sbgl_bridge_win64.dll") -Force
Copy-Item (Join-Path $PSScriptRoot "source_bsp_geometry_lab\lua\autorun\client\cl_sbgl_live.lua") (Join-Path $luaDir "cl_sbgl_live.lua") -Force

Write-Host ""
Write-Host "SBGL Bridge installed:"
Write-Host "  Native module: $binDir\gmcl_sbgl_bridge_win64.dll"
Write-Host "  Lua adapter:   $luaDir\cl_sbgl_live.lua"
Write-Host "Restart Garry's Mod, launch the Qt app, and click Connect / show overlay."
Write-Host "If the server blocks clientside Lua/binary modules, the bridge cannot connect on that server."
