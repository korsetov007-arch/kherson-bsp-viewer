# Source BSP Geometry Lab — user manual / руководство пользователя

> Version note: live telemetry is decoded exclusively from the visible `cl_showpos` screen text. No game-side component is required.

# Русский

## 1. Запуск и язык

Соберите проект по инструкции в [README](../vision-prototype/README.md) и запустите `bsp-geometry.exe`. При первом запуске приложение выбирает язык по языку Windows, если настройка ещё не сохранена. В меню **Настройки → Язык** можно выбрать **Русский** или **English**. Выбор запоминается и применяется при следующем запуске.

## 2. Загрузка карты BSP

1. Откройте вкладку **Геометрия BSP**.
2. Нажмите **Загрузить Source BSP v20…** и выберите файл `.bsp`.
3. В сводке проверьте путь, версию, размер, число вершин/рёбер/граней и число распознанных sky-поверхностей.
4. Справа показана таблица lump-ов: название, размер в байтах, число записей и версия.

Если файл не загружается, проверьте, что это Source BSP версии 20. Другие версии этой сборкой не поддерживаются.

## 3. Исправление отображения скайбокса

По умолчанию флажок **Показывать поверхности скайбокса** выключен. Парсер ищет названия sky-материалов в texture-info, texture-data и строковых таблицах BSP; распознанные sky-грани не передаются в обычное отображение геометрии. Это помогает убрать огромные плоскости sky из обзора уровня.

Включите флажок, если нужно проверить сами sky-поверхности. Если огромная плоскость всё равно видна при выключенном флажке, вероятно, у неё другое имя материала или в карте отсутствуют подходящие метаданные. Такой случай нужно проверять по материалам конкретного BSP; классификация не заменяет полноценный Source-рендерер. Текстуры и оригинальный skybox этим просмотрщиком не рисуются.

## 4. Навигация по геометрии

- **Сверху XY**: показ полигонов на плоскости X/Y. Щёлкните по карте, чтобы вывести координаты.
- **3D-орбита**: удерживайте левую кнопку мыши и перемещайте мышь для вращения; колесо масштабирует изображение до 24×; двойной щелчок сбрасывает камеру. В верхней строке отдельно показано число обычных BSP faces и displacement-треугольников, число реально отрисованных треугольников, записанных фрагментов и фрагментов, отклонённых depth buffer. Наведите курсор на окно 3D, чтобы увидеть мировые границы faces и displacement отдельно.
- Координата **Z** не определяется однозначно по виду сверху: одной точке X/Y могут соответствовать разные этажи. Введите нужную высоту вручную.
- Флажок sky-поверхностей меняет геометрию во всех связанных видах.

## 5. Баллистика

1. Перейдите на вкладку **Баллистика**.
2. Введите X/Y/Z точки запуска и цели. Кнопка **Выбрать X/Y на карте…** возвращает к виду сверху, чтобы выбрать горизонтальные координаты; Z остаётся ручным.
3. Для БМ-30 можно нажать **«БМ-30 · оценка HUD (4000 / 600)»**: 4000 ед./с взято из строки `Attachment.Ang:Forward() * 4000` в HUD аддона, а 600 — стандартное значение `sv_gravity` в Garry's Mod. Это **не подтверждённая скорость самой управляемой ракеты**; её параметры находятся во внешней сущности `sw_rocket_9m528_v3`.
4. Уточните гравитацию по `physenv.GetGravity()` для вашего сервера, если `sv_gravity` изменён. Настройте опорную скорость, азимут, угол возвышения и время полёта.
5. При включённом авторасчёте приложение задаёт прямой азимут между точками и выбирает низкую дугу с минимальным временем. Если геометрия BSP перекрывает её, приложение пробует высокую дугу. **Рассчитать текущую дугу** использует введённые углы вручную; **Решить для цели** пересчитывает решение.
6. **Очистить траекторию** убирает текущую кривую и маркер баллистической наводки.

Цвета: зелёный — запуск, красный — цель, янтарный — траектория.

## 6. Ограничения и устранение проблем

- Поддерживается только BSP v20.
- 3D-просмотр — программная перспективная отрисовка полигонов с per-pixel depth buffer и упрощённым направленным освещением; текстуры и оригинальный свет Source не используются. Статистика в заголовке позволяет отличить отсутствие геометрии от её перекрытия по глубине. Двойной щелчок сбрасывает положение камеры.
- Если sky-плоскости остаются, включите флажок и выключите обратно для сравнения; проверьте имена материалов или приложите карту для расширения правил определения.
- Баллистика не моделирует управляемость `sw_rocket_9m528_v3`, её фактическую скорость, сопротивление воздуха или физику внешнего аддона. Значение 4000 — лишь скорость HUD-предсказателя, а не доказанная скорость ракеты. Проверка препятствий использует только BSP-треугольники, без пропов и коллизий движка. Результаты — визуальный ориентир; приложение не управляет игрой.
- Если цель не имеет решения, увеличьте скорость, уменьшите гравитацию или выберите более близкую цель.

# English

## 1. Launch and language

Build the app using the [README instructions](../vision-prototype/README.md), then launch `bsp-geometry.exe`. On first start the app follows the Windows display language unless a preference was saved. Choose **Settings → Language → English / Русский** to change it. The preference is stored and restored on the next launch.

## 2. Load a BSP map

1. Open the **BSP geometry** tab.
2. Select **Load Source BSP v20…** and choose a `.bsp` file.
3. Review the summary for path, version, size, geometry counts and detected sky-textured faces.
4. The table on the right lists BSP lumps, their byte size, record count and version.

If loading fails, verify that the file is Source BSP version 20. Other versions are not supported by this build.

## 3. Skybox display

**Show skybox surfaces** is off by default. The parser reads material names from BSP texture-info, texture-data and string-table lumps. Faces using common sky material names are excluded from the standard geometry views, which helps prevent very large sky planes from dominating the preview.

Enable the checkbox when you want to inspect those surfaces. If a huge plane remains visible while the checkbox is off, it may use an unusual material name or the BSP may lack usable texture metadata. That needs verification against the specific map; this filter is not a replacement for a full Source renderer. The app does not render Source textures or the original skybox.

## 4. Geometry navigation

- **Top-down XY** shows polygons on the horizontal X/Y plane. Click the view to read coordinates.
- **Orbit 3D**: hold the left mouse button and drag to rotate; use the mouse wheel to zoom up to 24×; double-click to reset the camera. The header separately reports ordinary BSP faces and displacement triangles, triangles actually rasterized, written fragments and fragments rejected by the depth buffer. Hover over the 3D view to inspect face and displacement world bounds separately.
- **Z** cannot be inferred uniquely from a top-down view: the same X/Y may belong to different floors. Enter the desired height manually.
- The sky-surface checkbox affects the geometry shown in all linked views.

## 5. Ballistic lab

1. Open **Ballistic lab**.
2. Enter the launch and target X/Y/Z. **Pick XY on map…** switches to the top-down map so you can select X/Y; enter Z manually.
3. For the BM-30, you can choose **“BM-30 · HUD estimate (4000 / 600)”**. The 4000 units/s value comes from the addon's `Attachment.Ang:Forward() * 4000` HUD preview; 600 is the stock Garry's Mod `sv_gravity` value. This is **not a confirmed launch speed for the guided missile**; its parameters are defined in the external `sw_rocket_9m528_v3` entity.
4. Check `physenv.GetGravity()` for your server and adjust gravity if `sv_gravity` was changed. Set the reference speed, azimuth, elevation and flight time as needed.
5. With Auto-solve enabled, the app computes the direct XY azimuth and chooses the low-time ballistic arc. If BSP geometry blocks it, the high arc is tried. **Calculate current arc** uses manually entered angles; **Solve target** recomputes the solution.
6. **Clear path** removes the trajectory and ballistic aim marker.

Colors: green = launch, red = target, amber = trajectory.

## 6. Limitations and troubleshooting

- Only Source BSP v20 is supported.
- The 3D view is a lightweight perspective polygon preview without Source textures, lighting or a full Z-buffer. Double-click to reset the camera.
- If sky planes persist, toggle **Show skybox surfaces** to compare; material-name rules may need to be extended for the particular BSP.
- The model does not simulate `sw_rocket_9m528_v3` guidance, its actual speed, air drag or the external addon's physics. The value 4000 is only the HUD predictor speed, not a verified rocket speed. Obstruction checks use BSP triangles only, not props or engine collision. Results are visual references; the app does not control the game.
- If the solver reports no solution, increase speed, reduce gravity or select a closer target.


## Экранный резервный канал: cl_showpos / Screen fallback

### Русский

Этот режим не использует Lua, чтение памяти процесса или нейросеть. Он захватывает прямоугольную область основного монитора через DXGI Desktop Duplication и ищет видимые строки `pos:`, `ang:` и `vel:` через пиксельные шаблоны.

1. В игре включите `cl_showpos 1`, чтобы строки телеметрии были видны на экране.
2. Во вкладке «Баллистика» нажмите **«Выделить область мышью»**. Экран слегка затемнится; удерживайте левую кнопку и обведите прямоугольником строки `pos:` и `ang:` (по возможности и `vel:`), затем отпустите кнопку.
3. Область автоматически сохраняется в физических экранных пикселях относительно основного монитора и сразу запускает чтение. Для оконного или безрамочного режима результат надёжнее. При желании можно вручную уточнить X/Y/ширину/высоту.
4. Следите за состоянием распознавания и оценкой уверенности. Непрошедшие числовую проверку или низкоуверенные данные не принимаются.
5. Кнопка «Использовать pos как точку запуска» копирует прочитанную позицию в начало траектории. `pos` — это `MainViewOrigin` Source, а не гарантированная позиция дульного среза. Проверьте смещение камеры/орудия для конкретной техники.

Это экспериментальная реализация: распознавание шаблонов может зависеть от шрифта, разрешения, масштаба интерфейса и фона сцены. Низкоуверенные и некорректные данные отбрасываются. Работает только с текстом, который реально виден на выбранном экране.

### English

This mode does not use Lua, process-memory access or a neural network. It captures a crop from the primary display with DXGI Desktop Duplication and looks for visible `pos:`, `ang:` and `vel:` rows using pixel templates.

1. Enable `cl_showpos 1` in game, if the variable is available in that game build and server configuration.
2. In the Ballistic tab, click **Select crop with mouse**. The screen is dimmed; drag a rectangle around the visible `pos:` and `ang:` rows (include `vel:` if possible), then release the left mouse button.
3. The crop is saved in physical screen pixels relative to the primary monitor and the reader starts automatically. Windowed or borderless mode is recommended. X/Y/width/height can still be fine-tuned manually.
4. Check the reader status and confidence score. Invalid or low-confidence samples are rejected.
5. The Use last pos as launch point button copies the decoded position into the trajectory origin. `pos` is Source `MainViewOrigin`, not a guaranteed muzzle location; verify camera-to-weapon offset for the vehicle.

This is an experimental implementation. Recognition can vary with font rasterization, resolution, UI scaling and scene background. Invalid or low-confidence matches are rejected; only visible on-screen telemetry is used.



## Automatic fire control / Автоматическая СУО

### Русский
1. Во вкладке «Баллистика» задайте точку запуска и цель, скорость и гравитацию. Рассчитайте решение для цели. Для БМ-30 значение 4000 ед./с остаётся только оценкой из HUD аддона, а не подтверждённой скоростью ракеты.
2. Включите `cl_showpos 1`. В Geometry Lab включите чтение с экрана и выделите область со строками `pos:` и `ang:`. Дождитесь, пока появятся распознанные координаты и углы.
3. Переключитесь в Garry's Mod, встаньте в режим управления машиной и убедитесь, что поворот мыши управляет направлением наведения LVS. Нажмите F7.
4. При первом включении программа короткими движениями мыши калибрует фактическую связь «мышь → yaw/pitch» с учётом настроенной чувствительности. Затем по свежим данным `cl_showpos` она малыми шагами корректирует направление к рассчитанным азимуту и углу возвышения.
5. Повторное F7 немедленно отключает автодоводку и возвращает ручное управление. При потере фокуса Garry's Mod ввод мыши приостанавливается; после возврата в игру контроллер продолжает работу. Если поменять координаты или параметры баллистики, текущая автодоводка отключается до следующего F7.

**Ограничения:** контроллер управляет углами камеры через обычный относительный ввод мыши и читает `cl_showpos`; он не читает напрямую `cannon_yaw`/`cannon_pitch` из LVS и не подтверждает, что башня повторяет камеру в каждом режиме. Он работает только если LVS действительно связывает направление взгляда с наведением орудия и игра принимает синтетический ввод мыши. Если калибровка сообщает, что yaw/pitch не реагирует, приложение останавливается, а не продолжает посылать команды. В Garry's Mod переменная `m_rawinput` по умолчанию включена; она переключает игру на Raw Input, тогда как Windows `SendInput` создаёт синтетические события мыши в обычном потоке ввода. Если игра не видит тестовые движения, попробуйте вручную в консоли `m_rawinput 0`, затем снова нажмите F7. Приложение само не меняет эту переменную. Точная физика управляемой ракеты и её внешней сущности по-прежнему не моделируется.

F7 зарегистрирована как глобальная клавиша Windows. Если клавиша занята другим приложением, резервный вариант работает, когда активно окно Geometry Lab. Ввод мыши отправляется только при активном процессе Garry’s Mod (`gmod.exe` или `gmod_win64.exe` для актуальной 64-битной сборки).

### English
1. In Ballistic lab, set the launch point, target, speed and gravity, then solve for the target. For the BM-30, 4000 units/s remains an estimate copied from the addon's HUD predictor, not a verified missile speed.
2. Enable `cl_showpos 1`. Turn on the screen reader in Geometry Lab and select the visible `pos:` and `ang:` rows. Wait until decoded coordinates and angles appear.
3. Switch to Garry's Mod, enter the vehicle-control seat and verify that mouse look controls the LVS aiming direction. Press F7.
4. On first activation, the app uses small mouse movements to calibrate the actual mouse-to-yaw/pitch response at the current sensitivity. It then uses fresh `cl_showpos` samples to make small corrections toward the computed azimuth and elevation.
5. Press F7 again to immediately disable auto aim and restore manual control. Mouse input pauses when Garry's Mod loses focus and resumes when the game is foreground again. Changing ballistic coordinates or parameters stops the current auto aim until F7 is pressed again.

**Limitations:** the controller steers camera angles using ordinary relative mouse input and reads `cl_showpos`; it does not directly read LVS `cannon_yaw`/`cannon_pitch`, nor can it guarantee that the turret follows the camera in every mode. It works only when LVS maps view direction to gun aiming and the game accepts synthetic mouse input. If calibration reports that yaw/pitch did not respond, the app stops instead of continuing to inject input. Garry's Mod documents `m_rawinput` as enabled by default; it selects Raw Input, while Windows `SendInput` synthesizes mouse events into the normal input stream. If the game does not see the calibration movement, try `m_rawinput 0` manually in the game console and press F7 again. The app does not change this variable itself. Guided missile physics and the external rocket entity are still not simulated.

F7 is registered as a global Windows hotkey. If another application owns it, the fallback works while the Geometry Lab window is focused. Mouse input is sent only while the Garry’s Mod process is foreground (`gmod.exe` or `gmod_win64.exe` for the current 64-bit build).


## Heightfield / hillshade

### Русский

На вкладке «Геометрия BSP» включите **«Рельеф: heightfield / hillshade»**, чтобы по запросу растеризовать треугольники карты в сетку высот. Строится два слоя на XY-ячейку: `ground` — минимальная найденная высота вверх-ориентированных поверхностей; `canopy` — максимальная. Серый hillshade помогает визуально оценить рельеф.

Первая реализация использует размер ячейки 16 Source units (для крупных карт размер автоматически увеличивается, чтобы каждая сторона не превышала 2048 ячеек). Сетка предназначена для обзора и предварительных расчётов, а не является полной collision-моделью Source: вертикальные поверхности, закрытые внутренние пространства, некоторые ориентации граней и динамические/static props пока не моделируются. Используйте её как визуальный слой, не как доказательство проходимости или точной видимости.

При загрузке другой карты кэш высот сбрасывается; построение выполняется только после включения переключателя.

### English

On the BSP Geometry tab, enable **Hillshade heightfield** to rasterize map triangles into a height grid on demand. Each XY cell stores two layers: `ground` is the minimum sampled height from upward-facing surfaces, and `canopy` is the maximum. The grayscale hillshade provides a quick visual read of terrain shape.

The first implementation uses 16 Source units per cell (for large maps, the cell size automatically increases so no dimension exceeds 2048 cells). It is a visualization and preliminary analysis layer, not a complete Source collision model: vertical surfaces, enclosed multi-level spaces, some face orientations, and dynamic/static props are not fully represented. Do not treat it as proof of walkability or exact line-of-sight.

The cache resets when a new map is loaded; rasterization starts only when the checkbox is enabled.


## Проверка траектории на поверхности BSP / BSP surface trace

### Русский

После загрузки карты первый расчёт траектории лениво строит BVH по треугольникам поверхностей BSP (включая реконструированные displacement-треугольники и исключая sky). При пересечении траектория обрезается в первой найденной точке, а сводка показывает координаты, время и материал поверхности. При отсутствии пересечения показывается, что проверенные треугольники поверхности не перекрывают траекторию.

Важно: это быстрый тест по render-поверхностям BSP, а не точная коллизия движка. В него не входят модели пропов, специальные объёмы столкновений, все brush-solid семантики и серверные изменения. Отсутствие пересечения означает только отсутствие попадания в построенный треугольный набор.

### English

After a map is loaded, the first trajectory calculation lazily builds a BVH over BSP surface triangles (including reconstructed displacement triangles and excluding sky). If the path intersects a triangle, the rendered trajectory is clipped at the first hit and the summary reports position, time and surface material. If no intersection is found, the UI reports that the tested surface triangles do not block the path.

Important: this is a fast test against BSP render surfaces, not exact engine collision. Prop models, special collision volumes, all brush-solid semantics and server-side changes are not included. A clear trace only means the generated triangle set did not intersect the path.
