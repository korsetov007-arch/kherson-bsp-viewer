# Garry's Mod client addon / Клиентский аддон Garry's Mod

This is an **optional clientside addon** for Source BSP Geometry Lab. It writes local player/camera telemetry to the Garry's Mod DATA folder and reads the target/solution written by the desktop app. It does not need a server addon or server-side Lua.

Это **необязательный клиентский аддон** для Source BSP Geometry Lab. Он записывает локальную телеметрию игрока/камеры в папку DATA Garry's Mod и читает точку цели и расчёт из настольного приложения. Серверный аддон и серверный Lua не требуются.

## Install / Установка

1. Copy the folder `gmod-addon/source_bsp_geometry_lab` from this repository into your game's `garrysmod/addons/` directory. The final path should be:

   `GarrysMod/garrysmod/addons/source_bsp_geometry_lab/lua/autorun/client/cl_sbgl_live.lua`

2. Restart Garry's Mod (or use `lua_openscript_cl` in a local development session if you know what you're doing).

3. Start `bsp-geometry.exe`. In **Ballistic lab → Garry's Mod live link**, choose your game's `garrysmod/data` folder, for example:

   `.../steamapps/common/GarrysMod/garrysmod/data`

4. Load the matching `.bsp` map in the desktop app. Its base filename must match `game.GetMap()` in Garry's Mod; for example, `gm_flatgrass.bsp` matches `gm_flatgrass`.

5. Select a target on the BSP map, set target Z and the projectile speed/gravity values, then choose **Solve target · low arc** or **Solve target · high arc**. Once telemetry is available, the app updates the launch position and recomputes the selected ideal arc. The client HUD shows a screen marker for the desired view angles.

Скопируйте папку `gmod-addon/source_bsp_geometry_lab` из репозитория в `garrysmod/addons/`. Запустите игру и `bsp-geometry.exe`, затем во вкладке **Ballistic lab / Баллистика** нажмите **Select garrysmod/data folder / Выбрать папку garrysmod/data** и укажите папку `garrysmod/data` вашей установки. Загрузите BSP-файл той же карты, что открыта в игре (имя файла без расширения должно совпадать с `game.GetMap()`). Выберите точку цели, задайте её высоту Z и параметры снаряда, затем рассчитайте низкую или высокую дугу.

## HUD controls / Управление HUD

- `sbgl_hud_toggle` — show/hide the live marker.
- `sbgl_hud_enabled 1` — show the marker.
- `sbgl_hud_enabled 0` — hide the marker.

The marker position is derived from the desired yaw and pitch and the current client view angles. When the desired direction is outside the visible screen, the marker clamps to the screen edge. Amber means the desired direction is outside the view. Stale solution data is not displayed.

Команды консоли:
- `sbgl_hud_toggle` — показать/скрыть маркер.
- `sbgl_hud_enabled 1` — показать.
- `sbgl_hud_enabled 0` — скрыть.

Маркер показывает направление относительно текущих углов камеры. Если нужное направление вне экрана, маркер перемещается к краю. Янтарный цвет означает, что направление вне текущего поля зрения. Устаревшие расчёты не отображаются.

## Data exchange / Обмен данными

All exchange is local to the same computer. The addon writes:

- `garrysmod/data/source_bsp_geometry_lab/telemetry.json`

The desktop app writes:

- `garrysmod/data/source_bsp_geometry_lab/aim.json`

No network socket, server addon, or external service is used.

Весь обмен происходит локально на компьютере. Аддон записывает `telemetry.json`, а настольное приложение — `aim.json` в `garrysmod/data/source_bsp_geometry_lab/`. Сетевой сокет, серверный аддон или внешний сервис не используются.

## Important limitations / Ограничения

- This is a generic coordinate-based marker, not an engine-level weapon integration. `GetShootPos()` is the player's view position; custom artillery may fire from a different muzzle position or use its own camera/turret angles.
- Match the correct BSP/map and calibrate projectile speed and gravity for the specific weapon/mod. Source units, server-side projectile physics, drag, wind, collision, and muzzle offset may differ from this ideal model.
- Third-person and vehicle cameras can use custom view logic. The addon handles common vehicle angle conversion, but it cannot infer arbitrary scripted cameras or each weapon's private turret state.
- The HUD does not move the mouse, change angles, or fire. It only draws a local marker.
- Install and use clientside Lua only where the game/server's rules allow it.

- Это универсальный маркер по координатам, а не встроенная интеграция конкретного орудия. `GetShootPos()` возвращает положение взгляда игрока; у артиллерии точка выхода снаряда и углы башни могут быть другими.
- Нужна та же карта BSP и калибровка скорости/гравитации конкретного оружия. Реальная физика сервера, сопротивление воздуха, столкновения, ветер и смещение ствола в идеальной модели не учитываются.
- Пользовательские камеры и турели могут иметь собственную логику; универсальный аддон не может угадать скрытое состояние любого scripted weapon.
- HUD не двигает мышь, не изменяет углы и не стреляет автоматически. Он только рисует локальный маркер.
- Используйте клиентские Lua-скрипты в соответствии с правилами игры и сервера.
