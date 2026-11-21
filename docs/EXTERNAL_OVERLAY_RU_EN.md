# External overlay / Внешний оверлей

This mode draws a transparent, click-through overlay from the Windows desktop app itself. It requires no Garry's Mod Lua addon and makes no server-side changes.

Этот режим рисует прозрачный оверлей непосредственно настольным приложением Windows. Lua-аддон Garry's Mod и изменения на сервере не нужны.

## Русский

### Подготовка

1. Соберите и запустите `bsp-geometry.exe`.
2. Загрузите BSP той карты, на которой вы находитесь, во вкладке **Геометрия BSP**.
3. Откройте вкладку **Баллистика**. В консоли игры выполните `getpos`. Скопируйте выведенную строку, содержащую `setpos X Y Z; setang PITCH YAW ROLL`, в буфер обмена и нажмите **Вставить getpos**. Если копирование неудобно, введите начальные координаты и углы вручную.
4. Убедитесь, что координаты запуска соответствуют месту, откуда должен лететь снаряд. У артиллерии позиция игрока, точка выхода снаряда и углы башни могут различаться — в таком случае поправьте X/Y/Z вручную.
5. На карте выберите X/Y точки цели; задайте её Z вручную. Укажите скорость и гравитацию, соответствующие конкретному орудию, затем нажмите **Решить для цели · низкая дуга** или **… высокая дуга**.

### Запуск оверлея

1. В группе **Внешний оверлей · без аддона** задайте начальные yaw/pitch, чувствительность игры, значения `m_yaw`, `m_pitch` и горизонтальный FOV.
2. Нажмите **Запустить оверлей**, затем переключитесь обратно в Garry's Mod. Окно оверлея не перехватывает клики и не должно забирать фокус.
3. Маркер показывает рассчитанное направление относительно текущей оценённой ориентации камеры. Зелёный означает, что направление попадает на экран; янтарный означает, что оно за границей видимой области. Угловые ошибки показываются в градусах.
4. Для остановки вернитесь в приложение и нажмите **Остановить оверлей**.

### Калибровка и ограничения

- Направление оценивается так: начальные углы из `getpos` + суммирование относительных перемещений мыши `WM_INPUT` с коэффициентами `sensitivity × m_yaw` и `sensitivity × m_pitch`.
- Это **не телеметрия внутренних углов игры**. Оценка может ошибаться, если игровые значения чувствительности отличаются, raw input отключён, вы открываете меню/консоль, используете прицел с отдельной чувствительностью, переключаетесь на другую камеру или управляетесь не мышью.
- Для повторной калибровки снова возьмите `getpos` и нажмите **Вставить getpos**, затем остановите и заново запустите оверлей.
- Оверлей использует FOV, введённый вручную. При изменении FOV или прицельного зума обновите значение.
- Обычно лучше использовать оконный режим или **borderless fullscreen**: эксклюзивный полноэкранный режим может перекрывать сторонние окна. Для игры на другом мониторе перед запуском переместите само приложение на этот монитор.
- Расчёт траектории идеализирован: он не учитывает коллизии, ветер, сопротивление воздуха, задержку, смещение ствола или физику пользовательского орудия. Точность определяется тем, насколько параметры совпадают с игрой.

## English

### Setup

1. Build and launch `bsp-geometry.exe`.
2. In **BSP geometry**, load the BSP for the map currently being played.
3. Open **Ballistic lab**. Run `getpos` in the game console and copy the output line containing `setpos X Y Z; setang PITCH YAW ROLL` to the clipboard. Click **Paste getpos**. You can also enter the coordinates and initial angles manually.
4. Verify that the launch coordinates represent the actual projectile launch point. Artillery may use a different muzzle position and turret angles from the player camera; adjust X/Y/Z manually if needed.
5. Pick the target X/Y on the map and enter target Z manually. Set speed and gravity for the specific weapon, then choose **Solve target · low arc** or **Solve target · high arc**.

### Start the overlay

1. In **External overlay · no addon**, enter initial yaw/pitch, game sensitivity, `m_yaw`, `m_pitch` and horizontal FOV.
2. Click **Start overlay**, then switch back to Garry's Mod. The overlay is click-through and should not take focus.
3. The marker shows the solved direction relative to the estimated current camera orientation. Green means the direction is inside the visible screen; amber means it is outside the view. Angular deltas are shown in degrees.
4. To stop, return to the desktop app and click **Stop overlay**.

### Calibration and limitations

- Direction is estimated as: initial angles from `getpos` + integrated relative `WM_INPUT` mouse deltas, scaled by `sensitivity × m_yaw` and `sensitivity × m_pitch`.
- This is **not the game's internal angle telemetry**. It may drift if the game's sensitivity variables differ, raw input is disabled, the console/menu is open, scoped sensitivity changes, the game uses a separate scripted camera, or the turret does not follow mouse-look directly. Mouse movement over the console or menus can also be counted while the game stays foreground; stop or recalibrate after that.
- To recalibrate, copy a fresh `getpos` output, click **Paste getpos**, then stop and restart the overlay.
- FOV is entered manually; update it if the game changes FOV or zoom.
- **Borderless fullscreen** is recommended. Exclusive fullscreen may cover external windows; an ordinary non-maximized game window will not line up with this full-screen overlay. Move the desktop app to the same monitor as the game before starting it.
- The trajectory is idealized and ignores collisions, wind, drag, latency, muzzle offset and custom weapon physics; accuracy depends on the entered parameters.

