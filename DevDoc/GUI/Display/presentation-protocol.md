# Протокол вывода displayserv v1

## Назначение

Протокол определяет обмен с `displayserv` через
`/run/punos/displayserv.sock`: обнаружение выходов, чтение их состояния,
настройку режима, передачу итогового кадра и события hotplug/present.

Основной привилегированный клиент — `compositorserv`. Диагностический клиент
может читать список выходов и подписываться на изменения, но не может менять
режим или показывать кадры.

Все сообщения используют [базовый протокол GUI](../Common/protocol-base.md) и
[общие типы](../Common/common-types.md). Низкоуровневое взаимодействие с
устройством определено контрактом [displaylib](displaylib.md).

## Модель данных

```text
DisplayId
├── generation
├── connected / enabled
├── physical size
├── supported modes
└── current mode
      ↑
compositorserv ── present(frame FD) ──→ displayserv ──→ displaylib
```

`DisplayId` действует до `display.removed` или перезапуска `displayserv`.
Каждое изменение свойств выхода увеличивает `generation`. Изменяющий запрос
с устаревшим поколением отклоняется, поэтому клиент не применяет конфигурацию
к уже заменённому состоянию устройства.

## Авторизация presenter

`initserv` создаёт 32 случайных байта `PresenterToken` и передаёт по одной
копии `displayserv` и `compositorserv` через защищённые startup arguments.
Токен не хранится на диске, не передаётся через environment и не журналируется.

`compositorserv` выполняет `display.bind_presenter` после подключения. Только
успешно назначенный presenter может отправлять `display.configure` и
`display.present`. Одновременно допускается один presenter.

При отключении presenter displayserv закрывает принятые FD и завершает
ожидающие операции. Физический выход сохраняет последний успешно показанный
кадр, если backend это поддерживает; иначе становится чёрным до нового
presenter. Настройка режима не сбрасывается автоматически.

Это временная авторизация v1 до появления достоверных peer credentials.

## Типы wire format

### DisplayMode

Один режим занимает 12 байт:

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `0` | `width_px` | `u32` | Больше `0` |
| `4` | `height_px` | `u32` | Больше `0` |
| `8` | `refresh_millihz` | `u32` | Больше `0`; `60000` означает 60 Гц |

Режимы сравниваются по всем трём полям.

### DisplayOrientation

Поле кодируется как `u8`:

| Значение | Имя | Поворот по часовой стрелке |
| --- | --- | --- |
| `0` | `NORMAL` | 0° |
| `1` | `RIGHT` | 90° |
| `2` | `INVERTED` | 180° |
| `3` | `LEFT` | 270° |

### DisplayFlags

| Бит | Имя |
| --- | --- |
| `0x00000001` | `CONNECTED` |
| `0x00000002` | `ENABLED` |
| `0x00000004` | `PRIMARY` |

Неизвестные биты v1 должны быть нулевыми.

### DisplayInfo

`DisplayInfo` имеет 44-байтовую фиксированную часть и переменный хвост:

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `0` | `display_id` | `u64` | Ненулевой |
| `8` | `generation` | `u64` | Ненулевой |
| `16` | `flags` | `u32` | `DisplayFlags` |
| `20` | `physical_width_mm` | `u32` | `0`, если неизвестно |
| `24` | `physical_height_mm` | `u32` | `0`, если неизвестно |
| `28` | `current_mode` | `DisplayMode` | Нули, если выход выключен |
| `40` | `name_length` | `u16` | `1…128` байт UTF-8 |
| `42` | `mode_count` | `u16` | `0…128` |
| `44` | `name` | `u8[name_length]` | UTF-8 без NUL |
| `44 + name_length` | `modes` | `DisplayMode[mode_count]` | Без повторов |

Полный размер равен `44 + name_length + mode_count * 12`. Current mode обязан
присутствовать в `modes`, если выход подключён и включён.

### DisplayDamageRect

Прямоугольник повреждения кадра занимает 16 байт: `i32 x`, `i32 y`,
`u32 width`, `u32 height`. Координаты заданы в физических пикселях итогового
кадра. Сервис обрезает регион по границам кадра и проверяет переполнение.

### PresentFlags

| Бит | Имя | Значение |
| --- | --- | --- |
| `0x00000001` | `FULL_DAMAGE` | Повреждён весь кадр; `damage_count = 0` |

## Таблица сообщений

| `message_type` | Имя | Направление | Payload | FD |
| --- | --- | --- | --- | --- |
| `1` | `display.list` | Клиент → сервис | `VersionedRequest` | `0` |
| `2` | `display.list_result` | Сервис → клиент | `DisplayListResult` | `0` |
| `3` | `display.get_state` | Клиент → сервис | `DisplayIdRequest` | `0` |
| `4` | `display.get_state_result` | Сервис → клиент | `DisplayStateResult` | `0` |
| `5` | `display.configure` | Presenter → сервис | `DisplayConfigureRequest` | `0` |
| `6` | `display.configure_result` | Сервис → presenter | `DisplayConfigureResult` | `0` |
| `7` | `display.present` | Presenter → сервис | `DisplayPresentRequest` | `1` |
| `8` | `display.present_result` | Сервис → presenter | `BasicResult` | `0` |
| `9` | `display.subscribe` | Клиент → сервис | `VersionedRequest` | `0` |
| `10` | `display.subscribe_result` | Сервис → клиент | `BasicResult` | `0` |
| `11` | `display.added` | Сервис → подписчик | `DisplayInfoEvent` | `0` |
| `12` | `display.removed` | Сервис → подписчик | `DisplayRemovedEvent` | `0` |
| `13` | `display.changed` | Сервис → подписчик | `DisplayInfoEvent` | `0` |
| `14` | `display.presented` | Сервис → presenter | `DisplayPresentedEvent` | `0` |
| `15` | `display.present_failed` | Сервис → presenter | `DisplayPresentFailedEvent` | `0` |
| `16` | `display.backend_lost` | Сервис → подписчик | `DisplayBackendLostEvent` | `0` |
| `17` | `display.bind_presenter` | `compositorserv` → сервис | `PresenterBindRequest` | `0` |
| `18` | `display.bind_presenter_result` | Сервис → `compositorserv` | `BasicResult` | `0` |

## Схемы запросов

Все запросы начинаются с 10-байтового `RequestPrefix`:
`u16 protocol_version`, затем `u64 request_id`.

### VersionedRequest

Предметных полей нет. Полный размер — 10 байт.

### DisplayIdRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `display_id` | `u64` |

Полный размер — 18 байт.

### PresenterBindRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `presenter_token` | `u8[32]` |

Полный размер — 42 байта. После трёх неверных попыток соединение закрывается.

### DisplayConfigureRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `display_id` | `u64` | Живой выход |
| `18` | `generation` | `u64` | Совпадает с текущим поколением |
| `26` | `enabled` | `u8` | `0` или `1` |
| `27` | `orientation` | `u8` | `DisplayOrientation` |
| `28` | `mode` | `DisplayMode` | Поддерживаемый режим; нули при `enabled = 0` |

Полный размер — 40 байт. Успешная настройка увеличивает generation, инвалидирует
queued frames старого поколения и создаёт `display.changed`.

### DisplayPresentRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `display_id` | `u64` | Включённый выход |
| `18` | `generation` | `u64` | Текущее поколение |
| `26` | `width` | `u32` | Равно ширине current mode после orientation |
| `30` | `height` | `u32` | Равно высоте current mode после orientation |
| `34` | `stride` | `u32` | Не меньше `width * 4` |
| `38` | `pixel_format` | `u32` | `XRGB8888` или `ARGB8888` |
| `42` | `byte_size` | `u64` | Не меньше `stride * height` |
| `50` | `flags` | `u32` | `PresentFlags` |
| `54` | `damage_count` | `u16` | `0…64` |
| `56` | `damage` | `DisplayDamageRect[damage_count]` | Ровно `damage_count * 16` байт |

Размер равен `56 + damage_count * 16`; сообщение несёт ровно один ancillary
FD. При `FULL_DAMAGE` список обязан быть пустым. Пустой список без флага
допустим и означает, что пиксели не изменились, но кадр всё равно участвует в
очереди present.

## Схемы ответов

Каждый result начинается с 12-байтового `ResultPrefix`: `u32 error`, затем
`u64 request_id`.

### BasicResult

Предметных полей нет. Полный размер — 12 байт.

### DisplayListResult

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `12` | `snapshot_serial` | `u64` | Последний state-event serial, вошедший в снимок; `0` при ошибке |
| `20` | `display_count` | `u16` | `0…32`; `0` при ошибке |
| `22` | `displays` | `DisplayInfo[display_count]` | Последовательные variable-size записи |

### DisplayStateResult

При `CORE_OK` после `ResultPrefix` находится один `DisplayInfo`, начиная со
смещения `12`. При ошибке payload заканчивается на смещении `12`.

### DisplayConfigureResult

При `CORE_OK` после `ResultPrefix` находится применённый `DisplayInfo` с новым
generation. При ошибке payload заканчивается на смещении `12`.

`display.present_result(CORE_OK)` означает только принятие кадра в очередь.
Физический результат приходит отдельным completion event.

## Схемы событий

### DisplayInfoEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `display` | `DisplayInfo` |

### DisplayRemovedEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `display_id` | `u64` |
| `16` | `last_generation` | `u64` |

Полный размер — 24 байта.

### DisplayPresentedEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `request_id` | `u64` |
| `8` | `display_id` | `u64` |
| `16` | `generation` | `u64` |
| `24` | `presented_at_ns` | `u64` |

Полный размер — 32 байта. Событие является финальным результатом принятого
`display.present` и разрешает displayserv закрыть импортированный frame.

### DisplayPresentFailedEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `request_id` | `u64` |
| `8` | `display_id` | `u64` |
| `16` | `generation` | `u64` |
| `24` | `error` | `u32` |

Полный размер — 28 байт. `error` не может быть `CORE_OK`.

### DisplayBackendLostEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `error` | `u32` |

Полный размер — 12 байт. После события все DisplayId недействительны; каждый
принятый present завершается `display.present_failed`.

## Подписка и начальный снимок

`display.subscribe_result(CORE_OK)` устанавливает подписку для соединения.
Чтобы не потерять изменение между `list` и `subscribe`, клиент выполняет:

```text
display.subscribe
  → subscribe_result
  → display.list
  → list_result
  → применяет накопленные events с serial > snapshot_serial
```

Клиент буферизует state events до получения list result, устанавливает снимок,
отбрасывает события с `serial <= snapshot_serial`, затем применяет оставшиеся
по возрастанию serial. Повторный subscribe возвращает
`CORE_ALREADY_EXISTS`.

## Жизненный цикл present

```text
display.present + FD
        ↓
проверка payload, generation и backing storage
        ↓
display.present_result
   ├── error → FD закрыт, операция завершена
   └── CORE_OK
          ↓
       Queued → Presenting
          ↓
   presented / present_failed
          ↓
       FD и frame освобождены
```

Для одного DisplayId допускается один frame в backend и один frame в очереди
displayserv. Если оба места заняты, новый present получает
`CORE_WOULD_BLOCK`, а полученная копия FD закрывается.

Presenter сохраняет свой исходный FD. Displayserv принимает во владение
полученную копию только при `present_result(CORE_OK)` и закрывает её ровно один
раз после completion event или аварийного завершения операции.

Damage является подсказкой оптимизации. Backend может перерисовать весь кадр,
но не может читать пиксели за пределами проверенного backing storage.

## Configure и hotplug

1. `display.added` вводит новый DisplayId и его первое generation.
2. Любое изменение режима, orientation, enabled или свойств увеличивает
   generation и создаёт `display.changed`.
3. Present старого generation получает `CORE_INVALID_STATE`.
4. Configure завершает queued present старого generation событием
   `display.present_failed(CORE_INVALID_STATE)`.
5. `display.removed` является последним state event для DisplayId и завершает
   его presents с `CORE_NOT_FOUND`.
6. После configure compositor выполняет полный redraw нового размера.

## Ошибки

| Ситуация | `ErrorCode` |
| --- | --- |
| Неверный payload, bool, enum, mode или damage | `CORE_INVALID_ARGUMENT` |
| Неподдерживаемая версия, формат или orientation | `CORE_UNSUPPORTED` |
| Неверный PresenterToken или privileged request обычного клиента | `CORE_PERMISSION_DENIED` |
| Presenter/подписка уже назначены | `CORE_ALREADY_EXISTS` |
| DisplayId отсутствует или удалён | `CORE_NOT_FOUND` |
| Устаревший generation или выключенный выход | `CORE_INVALID_STATE` |
| Переполнение stride, размера или payload | `CORE_OVERFLOW` |
| Нельзя создать frame, список или очередь | `CORE_OUT_OF_MEMORY` |
| Очередь present или IPC заполнена | `CORE_WOULD_BLOCK` |
| Ошибка backend, mmap или present | `CORE_IO_ERROR` |

Повреждённый wire format, неверное число FD и неизвестный `message_type`
закрывают соединение по правилам базового протокола.

## Ограничения v1

- До 32 одновременных выходов и 128 режимов на выход.
- Только `XRGB8888` и premultiplied `ARGB8888`.
- Один presenter, один frame в backend и один queued frame на выход.
- Нет HDR, color management, variable refresh rate, explicit fences,
  hardware overlays и direct scanout.
- Нет постоянного display layout; состояние теряется при перезапуске.
- Передача frame FD требует расширения `ipclib` через `sendmsg/recvmsg`.
