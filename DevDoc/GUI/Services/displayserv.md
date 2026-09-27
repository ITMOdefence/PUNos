# displayserv

## Назначение

`displayserv` предоставляет остальному GUI системный доступ к физическим
дисплеям. Он перечисляет выходы и режимы, применяет конфигурацию и показывает
готовые кадры. Сервис не знает об окнах и не выполняет композицию.

| Поле | Значение |
| --- | --- |
| Идентификатор | `displayserv` |
| Исполняемый файл | `displayserv` |
| Владелец запуска | `initserv` |
| IPC endpoint | `/run/punos/displayserv.sock` |
| Версия протокола | `v1` |

## Зависимости

| Зависимость | Использование |
| --- | --- |
| `special/display` | Открытие backend, выходы, режимы и показ кадра |
| `special/ipc` | IPC endpoint и неблокирующие соединения |
| `sys/event` | Event loop backend и IPC |
| `sys/core`, `sys/memory` | Типы, ошибки и состояние клиентов |
| `logserv` | Запуск, hotplug, ошибки режимов и показа |

## Ответственность

- Единолично владеет `DisplayBackendHandle` и открытыми выходами.
- Назначает `DisplayId` и публикует актуальное поколение конфигурации.
- Проверяет режим и формат кадра до передачи backend.
- Сериализует configure и present для одного выхода.
- Не размещает окна, не хранит surface tree и не читает устройства ввода.

## Жизненный цикл

При запуске сервис создаёт event loop, открывает `displaylib`, перечисляет
выходы, создаёт IPC endpoint и переходит в `Ready`. Отсутствие выходов не
является ошибкой. Ошибка backend, event loop или endpoint завершает запуск.

При остановке сервис прекращает accept, отклоняет новые present, завершает
активные операции backend, закрывает соединения, backend и endpoint.

| Состояние | Переходы |
| --- | --- |
| `Starting` | `Ready`, `Failed` |
| `Ready` | `Reconfiguring`, `Stopping`, `Failed` |
| `Reconfiguring` | `Ready`, `Failed` |
| `Stopping` | `Stopped` |
| `Failed` | завершение процесса |

## Клиенты

| Клиент | Доступ |
| --- | --- |
| `compositorserv` | список выходов, configure, present и события |
| диагностика GUI | только список и состояние выходов |

До появления peer identity доступ ограничивается правами Unix socket.
Изменяющие операции предназначены только для `compositorserv`.

## Операции

| Операция | Запрос | Ответ | Назначение |
| --- | --- | --- | --- |
| `display.list` | `DisplayListRequest` | `DisplayListResponse` | Снимок подключённых выходов |
| `display.get_state` | `DisplayGetStateRequest` | `DisplayGetStateResponse` | Текущее состояние выхода |
| `display.configure` | `DisplayConfigureRequest` | `DisplayConfigureResponse` | Режим, ориентация и включение |
| `display.present` | `DisplayPresentRequest` + FD | `DisplayPresentResponse` | Поставить готовый кадр на вывод |

Основные поля:

```text
DisplayInfo = {
  DisplayId id, u64 generation, bool connected, bool enabled,
  u32 physical_width_mm, u32 physical_height_mm,
  DisplayMode current_mode, DisplayMode[] modes
}

DisplayMode = { u32 width_px, u32 height_px, u32 refresh_millihz }

DisplayPresentRequest = {
  u16 protocol_version, u64 request_id, DisplayId display,
  u64 generation, u32 width, u32 height, u32 stride,
  PixelFormat format, Rect[] damage
}
```

`generation` защищает от present в устаревшую конфигурацию после hotplug или
смены режима. Одновременно для выхода допускается не более одного кадра,
переданного backend и ещё не завершённого.

## События

| Событие | Данные | Условие |
| --- | --- | --- |
| `display.added` | `DisplayInfo` | Подключён новый выход |
| `display.removed` | `DisplayId`, `generation` | Выход отключён |
| `display.changed` | `DisplayInfo` | Режим или свойства изменились |
| `display.presented` | `DisplayId`, `request_id`, timestamp | Кадр показан |
| `display.present_failed` | `DisplayId`, `request_id`, `ErrorCode` | Показ невозможен |

После `display.removed` все ожидающие present этого выхода завершаются
`CORE_NOT_FOUND`. После configure клиент обязан сформировать полный кадр.

## Владение и потоковая модель

Сервис однопоточный: один event loop владеет backend, IPC и очередями present.
Полученный FD отображается только после полной валидации. `displayserv`
закрывает свою копию после `presented`, `present_failed` или отключения клиента.
Клиент сохраняет логическое владение исходным буфером.

## Ошибки

| Ошибка | Условие |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Размер, stride, формат или payload некорректен |
| `CORE_INVALID_STATE` | Выход выключен, reconfigure или уже занят |
| `CORE_NOT_FOUND` | Неизвестный `DisplayId` |
| `CORE_UNSUPPORTED` | Режим или формат не поддерживается |
| `CORE_WOULD_BLOCK` | Очередь present заполнена |
| `CORE_IO_ERROR` | Ошибка backend или IPC |

## Ограничения v1

- Один логический поток композиции на выход.
- Нет HDR, color management, variable refresh rate и hardware overlays.
- Нет постоянного хранения конфигурации дисплеев.
- Горячее подключение определяется возможностями конкретного backend.
