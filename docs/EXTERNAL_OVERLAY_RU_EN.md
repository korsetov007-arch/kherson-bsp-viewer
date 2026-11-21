# Native SBGL Bridge / Нативный мост SBGL

The old mouse-delta overlay has been removed. Camera angles are no longer estimated from Raw Input, and the app no longer asks for `getpos` clipboard paste.

Старый оверлей, оценивавший углы по движениям мыши, удалён. Программа больше не вычисляет углы из Raw Input и не просит копировать `getpos`.

## Setup / Установка

See [gmod-addon/native-bridge/README.md](../gmod-addon/native-bridge/README.md) for native module build instructions and Lua adapter setup.

См. [инструкцию по нативному мосту](../gmod-addon/native-bridge/README.md).

## Important limitations / Ограничения

- This uses the documented Garry's Mod binary Lua-module loader, not arbitrary DLL injection.
- Local client Lua and binary-module loading must be permitted by the GMod client/server configuration. The bridge cannot bypass servers that disable clientside Lua.
- The Lua adapter captures render-camera values in a render hook and exports best-effort LVS vehicle/weapon-handler data. Vehicle-specific barrel direction is not guaranteed by the common LVS API.
- The source is not a prebuilt binary. Compile the module and Qt app locally and test in-game before relying on it.

- Используется штатная загрузка бинарного Lua-модуля Garry's Mod, а не произвольная инъекция DLL.
- Клиентский Lua и бинарные модули должны быть разрешены конфигурацией игры/сервера. Мост не обходит серверные запреты.
- Lua-адаптер получает параметры камеры в render hook и экспортирует доступные данные LVS. Направление ствола конкретной машины не гарантируется общим API LVS.
- В репозитории находится исходный код, а не готовая проверенная DLL. Сначала соберите приложение и модуль и проверьте их в игре.
