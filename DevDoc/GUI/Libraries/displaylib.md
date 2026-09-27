# displaylib

## Назначение

`displaylib` изолирует `displayserv` от конкретного Linux graphics backend.
Она открывает backend, перечисляет выходы и режимы, применяет конфигурацию и
показывает импортированный кадр. Библиотека не предоставляет API приложениям.

| Поле | Значение |
| --- | --- |
| Имя | `special/display` |
| Публичный заголовок | `<special/display.h>` |
| Потребитель | только `displayserv` |

## Объекты

| Тип | Владение |
| --- | --- |
| `DisplayBackendHandle` | Создаёт `display_backend_open`, закрывает `display_backend_close` |
| `DisplayOutputHandle` | Принадлежит backend; не закрывается отдельно |
| `DisplayFrameHandle` | Временный импорт FD до завершения present |
| `DisplayEvent` | Заимствован на время чтения события |

## Операции

```text
display_backend_open(config, out_backend)
display_backend_close(backend)
display_backend_get_fd(backend, out_fd)
display_backend_dispatch(backend)

display_backend_list_outputs(backend, out_outputs)
display_output_get_info(output, out_info)
display_output_configure(output, config)

display_frame_import(backend, descriptor, fd, out_frame)
display_frame_destroy(frame)
display_output_present(output, frame, damage, present_id)
display_backend_next_event(backend, out_status, out_event)
```

FD из `display_backend_get_fd()` является невладеющим и нужен только для
регистрации в `sys/event`. Imported frame владеет переданной библиотеке копией
FD до `display_frame_destroy()`.

## События backend

- `OutputAdded`, `OutputRemoved`, `OutputChanged`;
- `PresentComplete` с `present_id` и timestamp;
- `PresentFailed` с `present_id` и `ErrorCode`;
- `BackendLost`.

Все операции неблокирующие. Если backend не может немедленно принять present,
возвращается `CORE_WOULD_BLOCK`. Библиотека не создаёт worker threads.

## Контракты

1. Все операции возвращают `ErrorCode`; `CORE_OK` — единственный успех.
2. Output handle действителен до `OutputRemoved` или закрытия backend.
3. Configure инвалидирует ранее импортированные scanout assumptions, но не
   закрывает frame автоматически.
4. Библиотека проверяет backend ABI, но policy выбора режима принадлежит
   `displayserv`.
5. Библиотека не знает о WindowId, SurfaceId, фокусе и приложениях.

## Backend v1

Первая реализация может использовать framebuffer/DRM subset, доступный PUNos.
Конкретные ioctl и структуры остаются внутренними. Публичный контракт не
обещает hardware acceleration, atomic modesetting или overlays.
