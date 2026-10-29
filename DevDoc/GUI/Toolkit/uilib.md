## Описание

`uilib` — клиентская библиотека мобильного GUI PUNos. Она создаёт app view и
поверхности через `windowserv`/`compositorserv`, управляет shared-memory
буферами, подключает IPC к event loop `appframework` и передаёт приложению
configure, lifecycle, navigation и input events. Widget toolkit строится
поверх неё.

## Имя библиотеки

`gui/ui`

## Контракты

1. Библиотека работает в процессе приложения, не создаёт поток и IPC endpoint.
2. В процессе v1 существует один `UiContextHandle` на один `AppHandle`.
3. Основной объект приложения — `UiViewHandle`. Для совместимости wire ABI ему
   соответствует `WindowId`; desktop-семантика из этого не следует.
4. Обычный клиент создаёт `App`, `Dialog` или `Transient`. Home, system bars,
   overlays, lock и IME доступны только авторизованной shell-библиотеке.
5. `ui_view_publish()` сообщает о готовности view, но foreground назначает
   только shell policy. Клиент получает lifecycle callback.
6. Первый `Configure` задаёт viewport, integer scale, orientation и safe insets.
   До configure present запрещён. Configure подтверждается первым успешным
   commit подходящего размера.
7. Для edge-to-edge view фон можно рисовать во всём viewport; интерактивный
   content layout по умолчанию ограничивается safe area.
8. При переходе в Background библиотека завершает текущие touch sequences
   CANCEL-событием и прекращает запрашивать новые frames. Буферы можно хранить.
9. При возврате в Foreground приложение получает lifecycle event и полный
   redraw request, если сервис инвалидировал содержимое.
10. Touch — основной ввод. Pointer и hardware key являются дополнительными.
    Распознавание tap/long-press/pan/scroll выполняет toolkit, не `uilib`.
11. Системный Back приходит как `NavigationRequested`; Home приложению не
    доставляется и переводит view в Background.
12. `UiEvent`, строки и массивы невладеющие и действуют только до возврата
    callback.
13. Все операции и callbacks выполняются в потоке-владельце `AppHandle`.
14. Библиотека держит не менее двух буферов. Acquired mapping можно изменять
    только до успешного present; повторное использование разрешено после
    buffer release.
15. Damage накапливается до present. Первый кадр и кадр после recovery всегда
    используют full damage.
16. CloseRequested не уничтожает view автоматически: приложение отвечает
    Allow, Deny или Defer.
17. При потере compositor библиотека пересоздаёт surface/buffers и делает
    surface rebind. При потере windowserv повторно создаёт view с действующим
    SurfaceBindToken. Во время recovery состояние `Suspended`.
18. Библиотека не выбирает foreground, global geometry, layer или z-order, не
    читает устройства и не открывает display backend.

## Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `appframework` | Event loop и lifecycle процесса |
| `libcore` | Типы, `ErrorCode`, размеры и проверки |
| `libmem` | Context, view, event queue и buffer pool |
| `libstr` | UTF-8 title и строки конфигурации |
| `libmath` | Point, Size, Rect, Insets и clipping |
| `libeventloop` | Маски событий через appframework |
| `special/ipc` | Неблокирующие соединения и передача FD |
| `windowserv` | Configure, lifecycle, navigation и input |
| `compositorserv` | Surface, buffer import, commit и frame callbacks |

## Состояния

### Context

| Состояние | Операции | Переходы |
| --- | --- | --- |
| `Ready` | create, dispatch, shutdown | `Suspended`, `Stopping` |
| `Suspended` | dispatch, reconnect, destroy | `Ready`, `Stopping` |
| `Stopping` | cleanup | `Destroyed` |
| `Destroyed` | Нет | Нет |

### View

| Состояние | Операции | Переходы |
| --- | --- | --- |
| `Created` | ожидание configure, destroy | `Configured`, `Suspended` |
| `Configured` | publish, acquire, present | `Background`, `Suspended` |
| `Background` | acquire/present по frame request, destroy | `Foreground`, `Hidden`, `Suspended` |
| `Foreground` | input, acquire, present, dialog | `Background`, `Suspended`, `Closing` |
| `Hidden` | повторный publish, destroy | `Background`, `Suspended` |
| `Suspended` | destroy, recovery | `Created`, `Configured` |
| `Closing` | сохранение и destroy | `Destroyed` |
| `Destroyed` | Нет | Нет |

### Buffer

`Free → Acquired → Submitted → Free`; при отмене `Acquired → Free`, при
потере сервиса любой buffer становится `Destroyed`.

## Типы

| Тип | Содержание |
| --- | --- |
| `UiContextHandle` | Контекст GUI приложения |
| `UiViewHandle` | App/dialog/transient view и связанная surface |
| `UiBufferHandle` | Элемент buffer pool view |
| `UiConfig` | preferred buffer count `2…4` |
| `UiViewConfig` | title, role, parent, flags, callback, user data |
| `UiViewRole` | `App`, `Dialog`, `Transient` |
| `UiLifecycle` | `Hidden`, `Background`, `Foreground`, `Suspended`, `Closing` |
| `UiOrientation` | `Normal`, `90`, `180`, `270` |
| `UiInsets` | top, right, bottom, left в logical units |
| `UiConfigure` | viewport, scale, orientation, safe insets, serial |
| `UiNavigationAction` | `Back` (`Home` остаётся у shell) |
| `UiEventKind` | configure, lifecycle, navigation, close, touch, pointer, key, frame, service |
| `UiMappedBuffer` | buffer, writable bytes, stride, pixel size, format |
| `UiEventHandler` | callback view + невладеющий event + user data |

## Константы

| Имя | Значение |
| --- | --- |
| `UI_BUFFER_COUNT_DEFAULT` | `2` |
| `UI_BUFFER_COUNT_MAX` | `4` |
| `UI_TITLE_MAX_BYTES` | `256` |
| `UI_DAMAGE_MAX_RECTS` | `64` |
| `UI_VIEW_EDGE_TO_EDGE` | `1` |
| `UI_VIEW_ACCEPTS_KEYBOARD` | `2` |
| `UI_VIEW_HANDLES_BACK` | `4` |
| `UI_MIN_TOUCH_TARGET` | `48` logical units (toolkit rule) |

## Операции

| Операция | Результат | Назначение |
| --- | --- | --- |
| `ui_initialize(app, config, out_context)` | `ErrorCode` | Подключает GUI-сервисы и event loop |
| `ui_view_create(context, config, out_view)` | `ErrorCode` | Создаёт surface и mobile view с rollback |
| `ui_view_publish(view)` | `ErrorCode` | Объявляет настроенное view готовым к shell activation |
| `ui_view_hide(view)` | `ErrorCode` | Отзывает view без уничтожения buffer pool |
| `ui_view_set_title(view, title)` | `ErrorCode` | Обновляет метаданные task switcher |
| `ui_view_respond_close(view, serial, response)` | `ErrorCode` | Allow/Deny/Defer закрытия |
| `ui_view_acquire_buffer(view, out_mapping)` | `ErrorCode` | Возвращает свободный writable buffer |
| `ui_view_damage(view, rects, count)` | `ErrorCode` | Накапливает logical damage |
| `ui_view_present(view, buffer)` | `ErrorCode` | Attach, configure ack и surface commit |
| `ui_view_destroy(view)` | `ErrorCode` | Удаляет view, surface и buffers |
| `ui_get_latest_configure(view, out_configure)` | `ErrorCode` | Возвращает viewport/orientation/insets |
| `ui_shutdown(context)` | `ErrorCode` | Закрывает view, IPC и event sources |

Shell использует отдельное привилегированное расширение над тем же wire
protocol: bind, create system roles, activate, set insets и navigate. Эти
операции не экспортируются обычному приложению.

## Ошибки

| `ErrorCode` | Когда |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | NULL, неверный UTF-8, role, flags, size или rect |
| `CORE_INVALID_STATE` | Операция запрещена lifecycle/context/buffer state |
| `CORE_NOT_FOUND` | Endpoint или server-side объект потерян |
| `CORE_PERMISSION_DENIED` | Privileged role или чужой parent/object |
| `CORE_OUT_OF_MEMORY` | Context, view, queue, backing storage или pool |
| `CORE_OVERFLOW` | Размер, stride, insets или damage превышает предел |
| `CORE_WOULD_BLOCK` | Нет buffer или заполнена IPC-очередь |
| `CORE_IO_ERROR` | Shared memory, IPC или повреждённое сообщение |
| `CORE_UNSUPPORTED` | Версия, формат, role или возможность не поддерживается |
