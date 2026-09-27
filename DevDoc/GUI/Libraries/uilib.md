# uilib

## Назначение

`uilib` — клиентская библиотека графического приложения PUNos. Она связывает
окна `windowserv`, поверхности `compositorserv` и event loop `appframework`,
управляет пулом графических буферов и доставляет события приложению.

| Поле | Значение |
| --- | --- |
| Имя | `uilib` |
| Публичный заголовок | `<gui/ui.h>` |
| Форма v1 | `libui.a` |
| Поток | поток-владелец `AppHandle` |

## Зависимости

| Зависимость | Использование |
| --- | --- |
| `appframework` | event loop и жизненный цикл приложения |
| `special/ipc` | соединения с windowserv и compositorserv |
| `sys/core`, `sys/memory`, `sys/math`, `sys/string` | типы и локальные объекты |
| `windowserv` | окна и события ввода |
| `compositorserv` | поверхности, буферы, damage и frame callbacks |

## Публичные объекты

| Тип | Владение |
| --- | --- |
| `UiContext` | Один на `AppHandle`; уничтожается `ui_shutdown()` |
| `UiWindow` | Создаётся приложением; связан с WindowId и SurfaceId |
| `UiBuffer` | Принадлежит пулу окна до release и уничтожения |
| `UiEvent` | Заимствован только на время callback |
| `UiSubscription` | Принадлежит окну до unsubscribe или destroy |

## Операции

```text
ui_initialize(app, config, out_context)
ui_shutdown(context)

ui_window_create(context, config, handler, user_data, out_window)
ui_window_show(window)
ui_window_hide(window)
ui_window_request_state(window, state)
ui_window_set_title(window, utf8_title)
ui_window_destroy(window)

ui_window_acquire_buffer(window, out_buffer, out_pixels, out_stride)
ui_window_damage(window, rects)
ui_window_present(window, buffer)
```

Все операции возвращают `ErrorCode`. Выходные параметры определены только при
`CORE_OK`. `ui_window_present()` выполняет attach, damage и commit; после
успешного вызова приложение не изменяет `pixels` до внутреннего
`buffer_released`.

## События приложения

```text
UiEvent = Configure | CloseRequested | FocusChanged |
          Pointer | Key | Touch | FrameDone | ServiceLost
```

Callback выполняется последовательно в event-loop потоке приложения и не
должен блокироваться. `Configure` содержит serial; библиотека подтверждает его
при первом present подходящего размера. `CloseRequested` не уничтожает окно
автоматически.

## Восстановление

При разрыве windowserv или compositorserv библиотека помечает объекты
`Suspended`, сообщает `ServiceLost` и пытается переподключиться. После
переподключения она создаёт новые server-side id и запрашивает полную
перерисовку. Автоматическое восстановление не сохраняет содержимое старых
буферов как нормативное состояние.

## Границы

`uilib` не запускает процессы, не владеет `AppHandle`, не читает физические
устройства, не определяет глобальное размещение окон и не вызывает display
backend. Widget toolkit строится поверх `uilib` отдельным модулем.

## Ограничения v1

- Один `UiContext` на приложение.
- Все вызовы и callbacks выполняются в потоке `AppHandle`.
- Только software buffers `XRGB8888` и `ARGB8888`.
- Clipboard, drag-and-drop, IME и accessibility tree будут расширениями.
