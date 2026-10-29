# Протокол мобильных представлений windowserv v1

## Назначение

Протокол определяет обмен между `uilib`, системной оболочкой и `windowserv`
через `/run/punos/windowserv.sock`. `WindowId` означает мобильное
представление: fullscreen app view, dialog или системный слой. Freeform-окон,
пользовательского перемещения, resize, minimize/maximize и desktop-декораций
в v1 нет.

Пиксели передаются по [surface protocol](../Compositor/surface-protocol.md),
сцена управляется по [scene protocol](../Compositor/scene-protocol.md), общие
wire-правила определены в [protocol-base](../Common/protocol-base.md).

## Модель объектов

```text
IPC-соединение приложения
└── WindowId
    ├── SurfaceId + SurfaceBindToken
    ├── role / flags / title
    ├── parent для dialog/transient
    ├── lifecycle state
    ├── pending configure
    └── confirmed viewport / scale / orientation / safe insets
```

Объект принадлежит создавшему соединению. Приложение не задаёт глобальную
позицию, размер, слой, z-order или foreground. Ими управляют `windowserv` и
авторизованная системная оболочка.

## Авторизация shell

Для ролей `HOME`, `SYSTEM_BAR`, `SYSTEM_OVERLAY`, `LOCK` и `IME`, а также
операций активации, системных insets и навигации нужен shell bind. `initserv`
создаёт 32 случайных байта `ShellToken` и передаёт их `windowserv` и оболочке
защищённым startup handoff. Одновременно существует одно shell-соединение.
Токен не хранится на диске, не попадает в environment и не журналируется.

## Перечисления и флаги

### WindowRole

| Значение | Имя | Parent | Доступ |
| --- | --- | --- | --- |
| `1` | `APP` | `0` | Любой клиент; основное fullscreen view |
| `2` | `DIALOG` | Живой `APP`/`DIALOG` того же клиента | Любой клиент |
| `3` | `TRANSIENT` | Живое окно того же клиента | Любой клиент |
| `4` | `HOME` | `0` | Только shell |
| `5` | `SYSTEM_BAR` | `0` | Только shell |
| `6` | `SYSTEM_OVERLAY` | `0` | Только shell |
| `7` | `LOCK` | `0` | Только shell |
| `8` | `IME` | `0` | Только shell; зарезервировано до IME |

### WindowLifecycle

| Значение | Имя | Смысл |
| --- | --- | --- |
| `1` | `HIDDEN` | Не опубликовано или отозвано клиентом |
| `2` | `BACKGROUND` | Живое app view без обычного ввода |
| `3` | `FOREGROUND` | Активное app view |
| `4` | `SUSPENDED` | Rendering временно остановлен сервисом |
| `5` | `CLOSING` | Новые кадры и ввод не принимаются |

Lifecycle назначает сервис. `window.show` публикует готовое view,
`window.hide` отзывает его, но клиент не может сам сделать себя foreground.

### WindowFlags

| Бит | Имя | Значение |
| --- | --- | --- |
| `0x1` | `EDGE_TO_EDGE` | Фон рисуется под system insets; контент учитывает safe area |
| `0x2` | `ACCEPTS_KEYBOARD` | View получает аппаратные key events |
| `0x4` | `HANDLES_BACK` | Back создаёт `window.navigation_requested` |

`ModifierMask`: `SHIFT=1`, `CONTROL=2`, `ALT=4`, `SUPER=8`,
`CAPS_LOCK=16`, `NUM_LOCK=32`.

## Таблица сообщений

Сообщения не несут FD.

| type | Имя | Направление | Payload |
| --- | --- | --- | --- |
| `1` | `window.create` | Клиент → сервис | `WindowCreateRequest` |
| `2` | `window.create_result` | Сервис → клиент | `WindowCreateResult` |
| `3` | `window.destroy` | Клиент → сервис | `WindowIdRequest` |
| `4` | `window.destroy_result` | Сервис → клиент | `BasicResult` |
| `5` | `window.show` | Клиент → сервис | `WindowIdRequest` |
| `6` | `window.hide` | Клиент → сервис | `WindowIdRequest` |
| `7` | `window.set_title` | Клиент → сервис | `WindowSetTitleRequest` |
| `8–9` | зарезервировано | — | Старые desktop-операции не используются |
| `10` | `window.ack_configure` | Клиент → сервис | `WindowAckConfigureRequest` |
| `11–12` | зарезервировано | — | Старые desktop-операции не используются |
| `13` | `window.set_modal` | Клиент → сервис | `WindowModalRequest` |
| `14` | `window.operation_result` | Сервис → клиент | `BasicResult` |
| `15` | `window.configure` | Сервис → клиент | `WindowConfigureEvent` |
| `16` | `window.lifecycle_changed` | Сервис → клиент | `WindowLifecycleEvent` |
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
| `27` | `shell.activate` | Shell → сервис | `ShellActivateRequest` |
| `28` | `shell.set_insets` | Shell → сервис | `ShellInsetsRequest` |
| `29` | `shell.navigate` | Shell → сервис | `ShellNavigateRequest` |
| `30` | `window.navigation_requested` | Сервис → клиент | `WindowNavigationEvent` |

Каждый запрос, кроме create и bind_shell, получает `window.operation_result`
с тем же `request_id`.

## Основные payload

Запрос начинается с `u16 protocol_version`, `u64 request_id`. Result начинается
с `u32 ErrorCode`, `u64 request_id`. Целые поля little-endian.

### WindowCreateRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `surface_id` | `u64` | Живая непривязанная поверхность |
| `18` | `surface_bind_token` | `u8[16]` | Токен поверхности |
| `34` | `role` | `u8` | `WindowRole` |
| `35` | `parent_id` | `u64` | Согласно роли |
| `43` | `flags` | `u32` | Только известные биты |
| `47` | `title_length` | `u16` | `0…256` |
| `49` | `title` | bytes | UTF-8 без NUL |

Размер равен `49 + title_length`. Начальный lifecycle — `HIDDEN`. Результат:
`u32 error`, `u64 request_id`, `u64 window_id`.

### WindowConfigureEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `window_id` | `u64` |
| `8` | `configure_serial` | `u64` |
| `16` | `width` | `u32` logical units |
| `20` | `height` | `u32` logical units |
| `24` | `scale` | `u32` 16.16 |
| `28` | `orientation` | `u8`: normal/90/180/270 |
| `29` | `safe_top` | `u32` |
| `33` | `safe_right` | `u32` |
| `37` | `safe_bottom` | `u32` |
| `41` | `safe_left` | `u32` |

Полный размер — 45 байт. App view получает размер дисплея; safe insets задают
область интерактивного контента. Fractional scale в v1 не выдаётся.

### Configure ack

`WindowAckConfigureRequest`: prefix, `u64 window_id`, `u64 configure_serial`.
Ack отправляется только после успешного surface commit нужного размера. До ack
остаётся предыдущий валидный кадр или системный фон.

### ShellActivateRequest

Prefix и `u64 window_id`. Цель — готовый `APP` или `HOME`. Сервис атомарно
переводит прежний foreground в `BACKGROUND`, цель — в `FOREGROUND`, обновляет
scene visibility и затем отправляет lifecycle events.

### ShellInsetsRequest

Prefix, `u64 display_id`, четыре `u32` inset: top, right, bottom, left.
Изменение создаёт новый configure для затронутых view.

### ShellNavigateRequest

Prefix, `u8 action`: `1=BACK`, `2=HOME`, `3=OVERVIEW`, затем `u64 target_id`
(`0` — текущий foreground). Home всегда обрабатывает shell. Back доставляется
приложению только при `HANDLES_BACK`; иначе policy активирует parent или Home.

### Остальные запросы

- `WindowIdRequest`: prefix + `u64 window_id`.
- `WindowSetTitleRequest`: prefix + id + `u16` length + UTF-8 title.
- `WindowModalRequest`: prefix + dialog id + parent id; `0` снимает модальность.
- `WindowSurfaceRebindRequest`: prefix + window id + surface id + token[16].
- `ShellBindRequest`: prefix + `ShellToken[32]`.
- `WindowCloseResponseRequest`: prefix + id + close serial + response
  (`ALLOW`, `DENY`, `DEFER`).

## Lifecycle и навигация

`WindowLifecycleEvent`: `u64 serial`, `u64 window_id`, `u8 lifecycle`,
`u8 reason`. Причины: shell activation, Home, lock, display loss и recovery.

`WindowNavigationEvent`: `u64 serial`, `u64 window_id`, `u8 action`. В v1 Back
является уведомлением: приложение изменяет navigation stack. Последующий Back
обрабатывается как новая операция.

## Ввод

`WindowTouchEvent` содержит window id, input serial, monotonic timestamp,
touch id, phase (`DOWN`, `MOVE`, `UP`, `CANCEL`) и локальные x/y в 24.8.
Sequence остаётся у получателя первого DOWN до UP/CANCEL. При смене lifecycle,
lock или потере устройства сначала отправляется CANCEL.

Pointer нужен QEMU, планшетным pointing devices и accessibility; он не
активирует background app. Key event получает foreground view с
`ACCEPTS_KEYBOARD`; Home/Power/Volume приложению не доставляются. Text input
требует отдельного IME-протокола. Подробности: [input-routing.md](input-routing.md).

## Жизненный цикл

```text
Created(HIDDEN) → Configured → Published(BACKGROUND)
                                      │ shell.activate
                                      ▼
                                 FOREGROUND
                                      │ Home/другое app/lock
                                      ▼
                                 BACKGROUND
                                      │ service/display loss
                                      ▼
                                  SUSPENDED

любое живое состояние → CLOSING → Destroyed
```

`window.show` публикует готовность, но не даёт foreground. Disconnect удаляет
окна соединения, modal links, touch ownership и scene bindings.

## Инварианты

1. На дисплее не более одного foreground `APP` или `HOME`.
2. `LOCK` блокирует обычный ввод и находится выше остальных слоёв.
3. Клиент не задаёт global geometry, stack или layer.
4. Только shell выполняет activate, системную навигацию и меняет insets.
5. Background view не получает обычный ввод и не обязано производить кадры.
6. Configure подтверждается кадром соответствующего размера.
7. Ошибка операции не оставляет частично применённой scene policy.

## Ошибки и ограничения v1

Используются `CORE_INVALID_ARGUMENT`, `CORE_INVALID_STATE`, `CORE_NOT_FOUND`,
`CORE_PERMISSION_DENIED`, `CORE_ALREADY_EXISTS`, `CORE_OUT_OF_MEMORY`,
`CORE_OVERFLOW`, `CORE_WOULD_BLOCK`, `CORE_IO_ERROR`, `CORE_UNSUPPORTED`.
Повреждённый wire format и неизвестный `message_type` закрывают соединение.

- Один display, один touch seat и одна пользовательская сессия.
- До 64 app view одного соединения и 512 объектов всего.
- Scale `1.0`; нет compositor-side rotation клиентского buffer.
- Нет split-screen, floating windows, picture-in-picture и desktop mode.
- Нет IME, clipboard, drag-and-drop и accessibility tree.
- Нет анимаций перехода; foreground меняется дискретно.
- Shell авторизуется startup token до появления peer credentials.
