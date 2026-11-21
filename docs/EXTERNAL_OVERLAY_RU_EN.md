# Screen-only cl_showpos telemetry / Экранная телеметрия cl_showpos

## Русский

Приложение использует только текст, который видно на экране; код внутриигрового модуля не требуется.

1. В Garry's Mod выполните \`cl_showpos 1\`, если команда доступна.
2. По возможности используйте оконный или borderless-режим. Запустите \`bsp-geometry.exe\`.
3. Откройте вкладку «Баллистика» и нажмите **«Выделить область мышью»**.
4. На затемнённом экране зажмите левую кнопку и обведите рамкой строки \`pos:\` и \`ang:\`; \`vel:\` можно включить в ту же область. Отпустите кнопку — выбранная область сразу сохранится и начнётся распознавание.
5. Чтобы поменять область, снова нажмите кнопку и выделите прямоугольник заново. Можно также вручную поправить X/Y/ширину/высоту. Значения сохраняются между запусками.
6. Убедитесь, что считываются координаты и углы, и проверяйте confidence. \`pos\` — начало вида Source, не обязательно дульный срез.

Селектор работает поверх основного монитора и переводит координаты выделения из логических единиц Qt в физические пиксели экрана. Из-за шрифтового сглаживания и яркого фона распознавание не гарантировано; при низкой уверенности данные не принимаются.

## English

The app uses only text that is visibly rendered on screen; no in-game module is required.

1. In Garry's Mod, run \`cl_showpos 1\` if the command is available.
2. Prefer windowed or borderless mode. Launch \`bsp-geometry.exe\`.
3. Open the Ballistic tab and click **Select crop with mouse**.
4. The screen dims. Hold the left mouse button and drag a rectangle around \`pos:\` and \`ang:\`; include \`vel:\` if possible. Release to save the crop and start recognition immediately.
5. Select again to change the crop, or fine-tune X/Y/width/height manually. The values persist between launches.
6. Check the decoded coordinates, angles and confidence. Source \`pos\` is the view origin, not necessarily the weapon muzzle.

The selector covers the primary monitor and converts Qt logical coordinates to physical display pixels. Font anti-aliasing and a bright scene can affect matching; low-confidence readings are rejected.
