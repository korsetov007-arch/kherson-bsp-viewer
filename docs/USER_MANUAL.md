# Source BSP Geometry Lab — user manual / руководство пользователя

> Version note: this manual describes the native Qt desktop application and its optional SBGL native bridge. The bridge receives telemetry through a local Windows named pipe and can request an in-game HUD marker. Setup and compatibility limitations are documented in [the native bridge guide](../gmod-addon/native-bridge/README.md).

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
- **3D-орбита**: удерживайте левую кнопку мыши и перемещайте мышь для вращения; колесо мыши меняет масштаб.
- Координата **Z** не определяется однозначно по виду сверху: одной точке X/Y могут соответствовать разные этажи. Введите нужную высоту вручную.
- Флажок sky-поверхностей меняет геометрию во всех связанных видах.

## 5. Баллистика

1. Перейдите на вкладку **Баллистика**.
2. Введите X/Y/Z точки запуска и цели. Кнопка **Выбрать X/Y на карте…** возвращает к виду сверху, чтобы выбрать горизонтальные координаты; Z остаётся ручным.
3. Настройте скорость (единиц в секунду), гравитацию (единиц в секунду²), азимут, угол возвышения и время полёта.
4. **Рассчитать текущую дугу** строит траекторию с введёнными параметрами.
5. **Решить для цели · низкая дуга** и **… высокая дуга** вычисляют идеальный угол, если цель достижима в выбранной модели.
6. **Очистить траекторию** убирает текущую кривую.

Цвета: зелёный — запуск, красный — цель, янтарный — траектория.

## 6. Ограничения и устранение проблем

- Поддерживается только BSP v20.
- 3D-просмотр — упрощённая отрисовка полигонов без текстур, света Source и полноценного Z-buffer.
- Если sky-плоскости остаются, включите флажок и выключите обратно для сравнения; проверьте имена материалов или приложите карту для расширения правил определения.
- Баллистика не учитывает столкновения, препятствия, сопротивление воздуха, взрывной радиус и поведение конкретного оружия. Результаты — только визуальный ориентир; приложение не управляет игрой.
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
- **Orbit 3D**: hold the left mouse button and drag to rotate; use the mouse wheel to zoom.
- **Z** cannot be inferred uniquely from a top-down view: the same X/Y may belong to different floors. Enter the desired height manually.
- The sky-surface checkbox affects the geometry shown in all linked views.

## 5. Ballistic lab

1. Open **Ballistic lab**.
2. Enter the launch and target X/Y/Z. **Pick XY on map…** switches to the top-down map so you can select X/Y; enter Z manually.
3. Set speed (units/s), gravity (units/s²), yaw/azimuth, elevation and flight time.
4. **Calculate current arc** draws a trajectory from the current values.
5. **Solve target · low arc** or **Solve target · high arc** calculates an ideal elevation angle when the target is reachable under the selected model.
6. **Clear path** removes the current curve.

Colors: green = launch, red = target, amber = trajectory.

## 6. Limitations and troubleshooting

- Only Source BSP v20 is supported.
- The 3D view is a lightweight polygon preview without Source textures, lighting or a full Z-buffer.
- If sky planes persist, toggle **Show skybox surfaces** to compare; material-name rules may need to be extended for the particular BSP.
- Ballistics do not include collisions, map occlusion, air drag, splash radius or weapon-specific behavior. The trajectory is a visual reference; the app does not control the game.
- If the solver reports no solution, increase speed, reduce gravity or select a closer target.


## Экранный резервный канал: cl_showpos / Screen fallback

### Русский

Этот режим не использует Lua, чтение памяти процесса или нейросеть. Он захватывает прямоугольную область основного монитора через DXGI Desktop Duplication и ищет видимые строки `pos:`, `ang:` и `vel:` через пиксельные шаблоны.

1. В игре включите `cl_showpos 1`, если эта переменная доступна в текущей сборке и на сервере.
2. Во вкладке «Баллистика» откройте «Резервный экранный канал», включите чтение и задайте X/Y/ширину/высоту области захвата.
3. Область задана в физических экранных пикселях относительно основного монитора. Она должна включать как минимум `pos:` и `ang:` строки. После изменения области нажмите «Применить область / запуск».
4. Следите за состоянием распознавания и оценкой уверенности. Непрошедшие числовую проверку или низкоуверенные данные не принимаются.
5. Кнопка «Использовать pos как точку запуска» копирует прочитанную позицию в начало траектории. `pos` — это `MainViewOrigin` Source, а не гарантированная позиция дульного среза. Проверьте смещение камеры/орудия для конкретной техники.

Это экспериментальная первая реализация: распознавание шаблонов ещё не проверено на реальном скриншоте GMod и может зависеть от шрифта, разрешения, масштаба интерфейса и фона сцены. Native Bridge предпочтительнее, когда он доступен.

### English

This mode does not use Lua, process-memory access or a neural network. It captures a crop from the primary display with DXGI Desktop Duplication and looks for visible `pos:`, `ang:` and `vel:` rows using pixel templates.

1. Enable `cl_showpos 1` in game, if the variable is available in that game build and server configuration.
2. In the Ballistic tab, expand Screen fallback, enable the reader, and set the crop X/Y/width/height.
3. Crop coordinates are physical screen pixels relative to the primary monitor. Include at least the `pos:` and `ang:` rows, then click Apply crop / start.
4. Check the reader status and confidence score. Invalid or low-confidence samples are rejected.
5. The Use last pos as launch point button copies the decoded position into the trajectory origin. `pos` is Source `MainViewOrigin`, not a guaranteed muzzle location; verify camera-to-weapon offset for the vehicle.

This is an experimental first implementation and has not been validated against an actual GMod screenshot. Recognition can vary with font rasterization, resolution, UI scaling and scene background. Prefer Native Bridge when available.
