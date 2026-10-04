# Оконный протокол windowserv v1

## Назначение

Протокол определяет обмен между `uilib` приложения и `windowserv` через
`/run/punos/windowserv.sock`: создание окон, конфигурацию размера, видимость,
состояния, модальность, интерактивное перемещение и доставку ввода.

Пиксели передаются отдельно по [протоколу поверхностей](../Compositor/surface-protocol.md),
а глобальная сцена управляется [протоколом сцены](../Compositor/scene-protocol.md).
Общие правила wire format определены в
[базовом протоколе GUI](../Common/protocol-base.md).

## Модель объектов

```text
IPC-соединение приложения
└── WindowId
    ├── SurfaceId + SurfaceBindToken
    ├── role / flags / title
    ├── parent / modal parent
    ├── requested state
    ├── pending configure
    └── confirmed geometry
```

`WindowId` принадлежит соединению, которое создало окно. Обычный клиент не
может управлять чужими окнами и не задаёт абсолютную позицию, z-order или слой.

## Авторизация shell

Обычные приложения используют протокол без предварительного bind. Для ролей
desktop/panel/lock `initserv` создаёт 32 случайных байта `ShellToken` и
передаёт их `windowserv` и процессу системной оболочки через защищённые startup
arguments.

Shell первым сообщением выполняет `window.bind_shell`. Одновременно существует
не более одного shell-соединения. Токен не хранится на диске, не передаётся
через environment и не журналируется. В v1 это временная замена peer
credentials.

## Перечисления и флаги

### WindowRole

Поле кодируется как `u8`:

| Значение | Имя | Parent | Доступ |
| --- | --- | --- | --- |
| `1` | `TOP_LEVEL` | Должен быть `0` | Любой клиент |
| `2` | `DIALOG` | Живое окно того же клиента | Любой клиент |
| `3` | `POPUP` | Живое окно того же клиента | Любой клиент |
| `4` | `DESKTOP` | Должен быть `0` | Только shell |
| `5` | `PANEL` | Должен быть `0` | Только shell |
| `6` | `LOCK` | Должен быть `0` | Только shell |

### WindowState

| Значение | Имя |
| --- | --- |
| `1` | `NORMAL` |
| `2` | `MINIMIZED` |
| `3` | `MAXIMIZED` |
| `4` | `FULLSCREEN` |

### WindowFlags

| Бит | Имя | Значение |
| --- | --- | --- |
| `0x00000001` | `RESIZABLE` | Пользователь может менять размер |
| `0x00000002` | `DECORATED` | Toolkit/shell рисует стандартные decorations |
| `0x00000004` | `ACCEPTS_FOCUS` | Окно может получить keyboard focus |
| `0x00000008` | `ALWAYS_ON_TOP` | Слой TOP; только shell или разрешённая policy |

### ResizeEdge

Поле `u8` является комбинацией: `LEFT = 1`, `RIGHT = 2`, `TOP = 4`,
`BOTTOM = 8`. Одновременно нельзя указывать `LEFT|RIGHT` или `TOP|BOTTOM`.

### ModifierMask

Поле `u32`: `SHIFT = 1`, `CONTROL = 2`, `ALT = 4`, `SUPER = 8`,
`CAPS_LOCK = 16`, `NUM_LOCK = 32`.

## Таблица сообщений

Сообщения не несут FD.

| `message_type` | Имя | Направление | Payload |
| --- | --- | --- | --- |
| `1` | `window.create` | Клиент → сервис | `WindowCreateRequest` |
| `2` | `window.create_result` | Сервис → клиент | `WindowCreateResult` |
| `3` | `window.destroy` | Клиент → сервис | `WindowIdRequest` |
| `4` | `window.destroy_result` | Сервис → клиент | `BasicResult` |
| `5` | `window.show` | Клиент → сервис | `WindowIdRequest` |
| `6` | `window.hide` | Клиент → сервис | `WindowIdRequest` |
| `7` | `window.set_title` | Клиент → сервис | `WindowSetTitleRequest` |
| `8` | `window.request_state` | Клиент → сервис | `WindowStateRequest` |
| `9` | `window.set_constraints` | Клиент → сервис | `WindowConstraintsRequest` |
| `10` | `window.ack_configure` | Клиент → сервис | `WindowAckConfigureRequest` |
| `11` | `window.begin_move` | Клиент → сервис | `WindowInteractiveRequest` |
| `12` | `window.begin_resize` | Клиент → сервис | `WindowResizeRequest` |
| `13` | `window.set_modal` | Клиент → сервис | `WindowModalRequest` |
| `14` | `window.operation_result` | Сервис → клиент | `BasicResult` |
| `15` | `window.configure` | Сервис → клиент | `WindowConfigureEvent` |
| `16` | `window.focus_changed` | Сервис → клиент | `WindowFocusEvent` |
| `17` | `window.close_requested` | Сервис → клиент | `WindowCloseEvent` |
| `18` | `window.visibility_changed` | Сервис → клиент | `WindowVisibilityEvent` |
| `19` | `window.pointer` | Сервис → клиент | `WindowPointerEvent` |
| `20` | `window.key` | Сервис → клиент | `WindowKeyEvent` |
| `21` | `window.touch` | Сервис → клиент | `WindowTouchEvent` |
| `22` | `window.rendering_suspended` | Сервис → клиент | `WindowServiceEvent` |
| `23` | `window.surface_rebind` | Клиент → сервис | `WindowSurfaceRebindRequest` |
| `24` | `window.bind_shell` | Shell → сервис | `ShellBindRequest` |
| `25` | `window.bind_shell_result` | Сервис → shell | `BasicResult` |
| `26` | `window.respond_close` | Клиент → сервис | `WindowCloseResponseRequest` |

Запросы `3`, `5–13`, `23` и `26` получают `window.operation_result` с тем же
`request_id`.

## Схемы запросов

Все запросы начинаются с 10-байтового `RequestPrefix`:
`u16 protocol_version`, затем `u64 request_id`.

### WindowCreateRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `surface_id` | `u64` | Живая непривязанная поверхность |
| `18` | `surface_bind_token` | `u8[16]` | Токен этой поверхности |
| `34` | `role` | `u8` | `WindowRole` |
| `35` | `parent_id` | `u64` | Согласно роли |
| `43` | `preferred_width` | `u32` | `1…16384` |
| `47` | `preferred_height` | `u32` | `1…16384` |
| `51` | `flags` | `u32` | Только известные `WindowFlags` |
| `55` | `title_length` | `u16` | `0…256` байт |
| `57` | `title` | `u8[title_length]` | UTF-8 без NUL |

Размер равен `57 + title_length`. Windowserv создаёт предварительный WindowId,
проверяет роль и parent, выполняет `scene.bind_window`, а затем публикует окно.
При ошибке scene binding предварительный объект удаляется.

### WindowIdRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |

Полный размер — 18 байт.

### WindowSetTitleRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Окно этого соединения |
| `18` | `title_length` | `u16` | `0…256` |
| `20` | `title` | `u8[title_length]` | UTF-8 без NUL |

### WindowStateRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Окно этого соединения |
| `18` | `state` | `u8` | `WindowState` |
| `19` | `display_id` | `u64` | Ненулевой только для `FULLSCREEN`; `0` означает выбор policy |

Полный размер — 27 байт. Успешный result означает принятие запроса policy;
новая конфигурация приходит отдельным `window.configure`.

### WindowConstraintsRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Окно этого соединения |
| `18` | `min_width` | `u32` | `0` или положительный размер |
| `22` | `min_height` | `u32` | `0` или положительный размер |
| `26` | `max_width` | `u32` | `0` означает без верхней границы |
| `30` | `max_height` | `u32` | `0` означает без верхней границы |

Полный размер — 34 байта. Ненулевой minimum не превышает соответствующий
ненулевой maximum. Constraints применяются к следующим configure.

### WindowAckConfigureRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |
| `18` | `configure_serial` | `u64` |

Полный размер — 26 байт. Клиент отправляет ack только после успешного
`surface.commit_result` для буфера размера, указанного configure.

### WindowInteractiveRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |
| `18` | `input_serial` | `u64` |

Полный размер — 26 байт. Serial должен принадлежать нажатой pointer-кнопке,
доставленной этому окну, и оставаться активным на момент запроса.

### WindowResizeRequest

Первые 26 байт совпадают с `WindowInteractiveRequest`.

| Смещение | Поле | Тип |
| --- | --- | --- |
| `26` | `edge_mask` | `u8` |

Полный размер — 27 байт.

### WindowModalRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Dialog этого соединения |
| `18` | `modal_parent_id` | `u64` | Parent этого соединения; `0` снимает модальность |

Полный размер — 26 байт. Цикл в parent/modal tree запрещён.

### WindowSurfaceRebindRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |
| `18` | `surface_id` | `u64` |
| `26` | `surface_bind_token` | `u8[16]` |

Полный размер — 42 байта. Операция разрешена только после
`window.rendering_suspended` и создаёт новый scene binding скрытым. После
успеха windowserv отправляет новый configure.

### ShellBindRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `shell_token` | `u8[32]` |

Полный размер — 42 байта. После трёх неверных попыток соединение закрывается.

### WindowCloseResponseRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Окно этого соединения |
| `18` | `close_serial` | `u64` | Serial активного `close_requested` |
| `26` | `response` | `u8` | `1 = ALLOW`, `2 = DENY`, `3 = DEFER` |

Полный размер — 27 байт. `ALLOW` переводит окно в `Closing`: новые операции и
ввод прекращаются, клиент завершает сохранение и отправляет `window.destroy`.
`DENY` отменяет запрос и оставляет окно рабочим. `DEFER` сохраняет запрос
активным; позже клиент повторяет response с тем же `close_serial`.

## Схемы ответов

Каждый result начинается с 12-байтового `ResultPrefix`: `u32 error`, затем
`u64 request_id`.

### BasicResult

Предметных полей нет. Полный размер — 12 байт.

### WindowCreateResult

| Смещение | Поле | Тип | Условие |
| --- | --- | --- | --- |
| `12` | `window_id` | `u64` | Определён только при `CORE_OK` |

Полный размер — 20 байт. При ошибке `window_id = 0`. Успешный result всегда
предшествует первому `window.configure` этого окна.

## Схемы событий окна

### WindowConfigureEvent

| Смещение | Поле | Тип | Значение |
| --- | --- | --- | --- |
| `0` | `configure_serial` | `u64` | Монотонен для окна |
| `8` | `window_id` | `u64` | Настраиваемое окно |
| `16` | `x` | `i32` | Глобальная координата 24.8 fixed-point |
| `20` | `y` | `i32` | Глобальная координата 24.8 fixed-point |
| `24` | `width` | `u32` | Размер клиентской области |
| `28` | `height` | `u32` | Размер клиентской области |
| `32` | `state` | `u8` | `WindowState` |
| `33` | `scale` | `u32` | 16.16 fixed-point; в v1 равно `65536` |
| `37` | `display_id` | `u64` | Основной display окна |

Полный размер — 45 байт. Координаты сообщаются для диагностики и scale/output
selection; обычный клиент не может их изменить.

### WindowFocusEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `focused` | `u8` |
| `17` | `reason` | `u8` |

Причины: `1 = POINTER`, `2 = KEYBOARD_POLICY`, `3 = WINDOW_HIDDEN`,
`4 = WINDOW_DESTROYED`, `5 = MODAL_CHANGED`.

### WindowCloseEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `reason` | `u8` |

Причины: `1 = USER`, `2 = SHELL`, `3 = PARENT_DESTROYED`,
`4 = SYSTEM_SHUTDOWN`. Событие является просьбой: клиент уничтожает окно сам.

### WindowVisibilityEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `visibility` | `u8` |

Значения: `0 = HIDDEN`, `1 = VISIBLE`, `2 = MINIMIZED`. Полная occlusion в v1
не сообщается, поскольку compositor не публикует её windowserv.

### WindowServiceEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `reason` | `u32` |

Причины: `1 = COMPOSITOR_DISCONNECTED`, `2 = SCENE_BINDING_LOST`.
После события клиент прекращает present и выполняет `surface_rebind` после
восстановления compositorserv.

## События ввода

`input_serial` монотонно возрастает в рамках запуска windowserv. Timestamp —
монотонное время обработки `IoInputFrame` в наносекундах; IOserv v1 не
передаёт аппаратный timestamp. DeviceId приложению не раскрывается.

### WindowPointerEvent

| Смещение | Поле | Тип | Значение |
| --- | --- | --- | --- |
| `0` | `input_serial` | `u64` | Serial события |
| `8` | `window_id` | `u64` | Получатель |
| `16` | `timestamp_ns` | `u64` | Монотонное время dispatch |
| `24` | `kind` | `u8` | Pointer kind |
| `25` | `button_state` | `u8` | `0 = released`, `1 = pressed` |
| `26` | `x` | `i32` | Локальная координата 24.8 fixed-point |
| `30` | `y` | `i32` | Локальная координата 24.8 fixed-point |
| `34` | `button_code` | `u32` | Linux `BTN_*`; `0`, если неприменимо |
| `38` | `axis_x` | `i32` | 16.16 fixed-point; `0`, если неприменимо |
| `42` | `axis_y` | `i32` | 16.16 fixed-point; `0`, если неприменимо |

Полный размер — 46 байт. Pointer kind: `1 = ENTER`, `2 = LEAVE`, `3 = MOTION`,
`4 = BUTTON`, `5 = AXIS`, `6 = CANCEL`.

### WindowKeyEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `input_serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `timestamp_ns` | `u64` |
| `24` | `key_code` | `u32` |
| `28` | `key_state` | `u8` |
| `29` | `modifiers` | `u32` |

Полный размер — 33 байта. `key_state`: `0 = released`, `1 = pressed`.
`key_code` — Linux `KEY_*`; Unicode и раскладка в v1 отсутствуют.

### WindowTouchEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `input_serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `timestamp_ns` | `u64` |
| `24` | `touch_id` | `u32` |
| `28` | `phase` | `u8` |
| `29` | `x` | `i32` |
| `33` | `y` | `i32` |

Полный размер — 37 байт. Phase: `1 = DOWN`, `2 = MOVE`, `3 = UP`,
`4 = CANCEL`. Touch sequence остаётся у окна первого `DOWN` до `UP/CANCEL`.

## Создание и первый кадр

```text
uilib: surface.create
  → SurfaceId + SurfaceBindToken
uilib: window.create(surface, token)
  → windowserv: scene.bind_window(HIDDEN)
  → window.create_result(WindowId)
  → window.configure(serial, size)
uilib: buffer.import + surface.attach + surface.commit
  → surface.commit_result(CORE_OK)
uilib: window.ack_configure(serial)
uilib: window.show
  → windowserv: scene.set_visibility(VISIBLE)
```

Окно не становится видимым до успешного ack и явного `window.show`.

## Configure и ack

1. Windowserv назначает serial и отправляет полный желаемый размер/state.
2. Новый configure заменяет предыдущий неподтверждённый configure.
3. Клиент создаёт/выбирает буфер точного размера, выполняет surface commit и
   ожидает `surface.commit_result(CORE_OK)`.
4. Клиент отправляет `window.ack_configure` с актуальным serial.
5. Windowserv применяет geometry/state через scene protocol.
6. Ack устаревшего serial возвращает `CORE_INVALID_STATE`.

До ack resize старое содержимое и geometry остаются действующими. При
minimize buffer можно сохранить, но SceneNode скрывается. Restore создаёт
новый configure.

## Focus, модальность и ввод

- Keyboard event получает одно активное окно с focus.
- Pointer event получает hit-test owner или текущий grab owner.
- Pointer press активирует верхнее окно, допускающее focus.
- Modal dialog блокирует pointer/keyboard input своему parent и его предкам.
- Hidden, Minimized, Closing и rendering-suspended окна ввод не получают.
- `begin_move/resize` принимает только serial активной pointer-кнопки этого
  окна и создаёт grab до release/cancel.
- При `SyncLost`, DeviceLost или разрыве IOserv windowserv отправляет cancel,
  очищает pressed keys/buttons, grabs и touch ownership.

Дополнительные правила определены в [маршрутизации ввода](input-routing.md).

## Жизненный цикл окна

```text
Created → Configured → Visible
   │          ↑          │
   │          └─ hide ───┘
   │                     ↓
   │                 Minimized
   │                     │ restore/configure
   └──────────────→ Closing → Destroyed
```

`close_requested` сам не меняет состояние окна. Переход в `Closing` происходит
после `window.respond_close(ALLOW)`. Сервис не завершает процесс приложения.
При аварийном disconnect все окна соединения, modal links, focus и grabs
удаляются автоматически.

## Ошибки

| Ситуация | `ErrorCode` |
| --- | --- |
| Неверный payload, UTF-8, role, state, flags или edge | `CORE_INVALID_ARGUMENT` |
| Неподдерживаемая версия или role | `CORE_UNSUPPORTED` |
| Неверный ShellToken/SurfaceBindToken или чужой объект | `CORE_PERMISSION_DENIED` |
| Shell уже назначен | `CORE_ALREADY_EXISTS` |
| WindowId, parent, SurfaceId или DisplayId отсутствует | `CORE_NOT_FOUND` |
| Устаревший configure/input serial или неверный переход | `CORE_INVALID_STATE` |
| Нельзя создать окно, title или policy state | `CORE_OUT_OF_MEMORY` |
| Размер, дерево или очередь выходит за пределы | `CORE_OVERFLOW` |
| IPC-очередь временно заполнена | `CORE_WOULD_BLOCK` |
| Ошибка IOserv/compositorserv | `CORE_IO_ERROR` |

Повреждённый wire format и неизвестный `message_type` закрывают соединение по
правилам базового протокола. Корректный запрос привилегированной роли без
shell bind возвращает `CORE_PERMISSION_DENIED`.

## Ограничения v1

- До 256 окон одного соединения и 4096 окон всего.
- Один workspace, один keyboard focus и один pointer seat.
- Scale всегда `1.0`; нет fractional scaling.
- Нет clipboard, drag-and-drop, IME, Unicode layout и global shortcuts.
- Нет server-side decorations, accessibility tree и persisted placement.
- Нет события полной occlusion.
- Shell авторизуется startup token до появления peer credentials.
