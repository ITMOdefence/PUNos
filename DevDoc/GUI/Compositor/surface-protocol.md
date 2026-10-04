# Протокол поверхностей compositorserv v1

## Назначение

Протокол определяет обмен между `uilib` приложения и `compositorserv` через
`/run/punos/compositorserv.sock`. Клиент создаёт поверхности, импортирует
shared-memory буферы, накапливает изменения и публикует их атомарным commit.

Протокол сцены, через который `windowserv` задаёт положение, видимость и
z-order поверхности, в этот документ не входит.

Все сообщения используют [базовый протокол GUI](../Common/protocol-base.md) и
[общие типы](../Common/common-types.md). Здесь приведены полные схемы
предметных полей после общих префиксов.

## Модель объектов

```text
IPC-соединение клиента
├── SurfaceId
│   ├── pending state
│   └── current state ──→ BufferId
└── BufferId ──→ imported shared-memory FD
```

`SurfaceId` и `BufferId` принадлежат создавшему соединению. Числовой
идентификатор не даёт другому соединению доступ к объекту.

В v1 размер поверхности равен размеру прикреплённого буфера, одна координатная
единица поверхности равна одному пикселю. Buffer scale, rotation, viewport,
subsurface и произвольный transform отсутствуют.

## Типы wire format

### BufferRect

Прямоугольник в пиксельных координатах прикреплённого буфера, 16 байт:

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `0` | `x` | `i32` | Может выходить за границу; сервис выполняет clipping |
| `4` | `y` | `i32` | Может выходить за границу; сервис выполняет clipping |
| `8` | `width` | `u32` | Больше `0` |
| `12` | `height` | `u32` | Больше `0` |

Сложение `x + width`, `y + height` и вычисление площади выполняются с
проверкой переполнения.

### PixelFormat

| Значение | Имя | Представление |
| --- | --- | --- |
| `1` | `XRGB8888` | B, G, R, X; 32 бита; непрозрачный |
| `2` | `ARGB8888` | B, G, R, A; 32 бита; premultiplied alpha |

Значение `0` и неизвестные значения возвращают `CORE_UNSUPPORTED`.

### SurfaceDamageFlags

| Бит | Имя | Значение |
| --- | --- | --- |
| `0x00000001` | `FULL_DAMAGE` | Повреждена вся поверхность; `rect_count` обязан быть `0` |

Остальные биты должны быть нулевыми.

## Таблица сообщений

Номера `1–19` принадлежат клиентскому протоколу поверхностей endpoint
`compositorserv`.

| `message_type` | Имя | Направление | Payload | FD |
| --- | --- | --- | --- | --- |
| `1` | `surface.create` | Клиент → сервис | `SurfaceCreateRequest` | `0` |
| `2` | `surface.create_result` | Сервис → клиент | `SurfaceCreateResult` | `0` |
| `3` | `surface.destroy` | Клиент → сервис | `SurfaceIdRequest` | `0` |
| `4` | `surface.destroy_result` | Сервис → клиент | `BasicResult` | `0` |
| `5` | `buffer.import` | Клиент → сервис | `BufferImportRequest` | `1` |
| `6` | `buffer.import_result` | Сервис → клиент | `BufferImportResult` | `0` |
| `7` | `buffer.destroy` | Клиент → сервис | `BufferIdRequest` | `0` |
| `8` | `buffer.destroy_result` | Сервис → клиент | `BasicResult` | `0` |
| `9` | `surface.attach` | Клиент → сервис | `SurfaceAttachRequest` | `0` |
| `10` | `surface.damage` | Клиент → сервис | `SurfaceDamageRequest` | `0` |
| `11` | `surface.set_opaque` | Клиент → сервис | `SurfaceRegionRequest` | `0` |
| `12` | `surface.request_frame` | Клиент → сервис | `SurfaceFrameRequest` | `0` |
| `13` | `surface.commit` | Клиент → сервис | `SurfaceCommitRequest` | `0` |
| `14` | `surface.commit_result` | Сервис → клиент | `BasicResult` | `0` |
| `15` | `surface.frame_done` | Сервис → клиент | `SurfaceFrameDoneEvent` | `0` |
| `16` | `surface.buffer_released` | Сервис → клиент | `BufferReleasedEvent` | `0` |
| `17` | `surface.output_enter` | Сервис → клиент | `SurfaceOutputEvent` | `0` |
| `18` | `surface.output_leave` | Сервис → клиент | `SurfaceOutputEvent` | `0` |
| `19` | `surface.full_redraw` | Сервис → клиент | `SurfaceRedrawEvent` | `0` |

## Схемы запросов

Все запросы начинаются с 10-байтового `RequestPrefix`:
`u16 protocol_version`, затем `u64 request_id`.

### SurfaceCreateRequest

Предметных полей нет. Полный размер payload — 10 байт.

### SurfaceIdRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `surface_id` | `u64` |

Полный размер — 18 байт.

### BufferImportRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `width` | `u32` | `1…16384` |
| `14` | `height` | `u32` | `1…16384` |
| `18` | `stride` | `u32` | Не меньше `width * 4` |
| `22` | `pixel_format` | `u32` | `XRGB8888` или `ARGB8888` |
| `26` | `byte_size` | `u64` | Не меньше `stride * height` |

Полный размер — 34 байта; сообщение несёт ровно один ancillary FD. Сервис
проверяет фактический размер backing storage, арифметику размеров и возможность
создать read-only non-executable mapping до создания `BufferId`.

### BufferIdRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `buffer_id` | `u64` |

Полный размер — 18 байт.

### SurfaceAttachRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `surface_id` | `u64` | Поверхность этого соединения |
| `18` | `buffer_id` | `u64` | Буфер этого соединения; `0` означает detach |

Полный размер — 26 байт. Attach изменяет только pending state. Один `BufferId`
может находиться в pending/current state не более чем одной поверхности.

### SurfaceDamageRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `surface_id` | `u64` | Поверхность этого соединения |
| `18` | `flags` | `u32` | Только `FULL_DAMAGE` или `0` |
| `22` | `rect_count` | `u16` | `0…64` |
| `24` | `rects` | `BufferRect[rect_count]` | Ровно `rect_count * 16` байт |

Размер равен `24 + rect_count * 16`. При `FULL_DAMAGE` список обязан быть
пустым. Без флага пустой список является допустимым no-op. Повторные damage до
commit объединяются; сервис может схлопнуть сложный регион в bounding rect.

### SurfaceRegionRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `surface_id` | `u64` | Поверхность этого соединения |
| `18` | `rect_count` | `u16` | `0…64` |
| `20` | `rects` | `BufferRect[rect_count]` | Внутри границ pending buffer |

Размер равен `20 + rect_count * 16`. Пустой список означает отсутствие
гарантированно непрозрачной области. Для `XRGB8888` сервис может считать весь
буфер непрозрачным независимо от этого региона.

### SurfaceFrameRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `surface_id` | `u64` |

Полный размер — 18 байт. `request_id` одновременно является идентификатором
frame callback. Запрос присоединяется к следующему успешному commit этой
поверхности. До одного commit допускается не более одного frame request.

### SurfaceCommitRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `surface_id` | `u64` | Поверхность этого соединения |
| `18` | `commit_serial` | `u64` | Ненулевой и больше предыдущего serial поверхности |

Полный размер — 26 байт.

## Схемы ответов

Каждый result начинается с 12-байтового `ResultPrefix`: `u32 error`, затем
`u64 request_id`.

### BasicResult

Предметных полей нет. Полный размер — 12 байт.

### SurfaceCreateResult

| Смещение | Поле | Тип | Условие |
| --- | --- | --- | --- |
| `12` | `surface_id` | `u64` | Определён только при `CORE_OK` |

Полный размер — 20 байт. При ошибке поле записывается как `0`.

### BufferImportResult

| Смещение | Поле | Тип | Условие |
| --- | --- | --- | --- |
| `12` | `buffer_id` | `u64` | Определён только при `CORE_OK` |

Полный размер — 20 байт. При ошибке поле записывается как `0`, полученная копия
FD закрывается сервисом.

## Схемы событий

### SurfaceFrameDoneEvent

| Смещение | Поле | Тип | Значение |
| --- | --- | --- | --- |
| `0` | `request_id` | `u64` | Идентификатор `surface.request_frame` |
| `8` | `surface_id` | `u64` | Поверхность callback |
| `16` | `commit_serial` | `u64` | Показанный commit или более новый commit этой поверхности |
| `24` | `presented_at_ns` | `u64` | Монотонное время завершения показа |

Полный размер — 32 байта. Событие разрешает клиенту планировать следующую
отрисовку, но не разрешает изменять использованный буфер.

### BufferReleasedEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `buffer_id` | `u64` |

Полный размер — 16 байт. После этого события compositor и display больше не
читают буфер, поэтому клиент может снова записывать в backing storage.

### SurfaceOutputEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `surface_id` | `u64` |
| `16` | `display_id` | `u64` |

Полный размер — 24 байта. `output_enter` сообщает начало пересечения с
display, `output_leave` — его окончание. Одна поверхность может одновременно
пересекать несколько display.

### SurfaceRedrawEvent

| Смещение | Поле | Тип | Значение |
| --- | --- | --- | --- |
| `0` | `serial` | `u64` | Serial события соединения |
| `8` | `surface_id` | `u64` | Поверхность |
| `16` | `reason` | `u32` | Причина полной перерисовки |

Полный размер — 20 байт. Причины v1: `1 = OUTPUT_CHANGED`,
`2 = CONTENT_LOST`, `3 = COMPOSITOR_RECOVERED`. Клиент должен отправить новый
буфер с `FULL_DAMAGE`; старое содержимое нельзя считать сохранённым.

## Pending и current state

У каждой поверхности существуют два набора состояния:

| Состояние | Содержимое |
| --- | --- |
| Pending | attach/detach, damage, opaque region и frame callback |
| Current | Буфер и opaque region, принятые последним успешным commit |

Операции `attach`, `damage`, `set_opaque` и `request_frame` не меняют current
state. `surface.commit` сначала проверяет весь pending state и только затем
атомарно заменяет current state.

Если commit возвращает ошибку:

- current state остаётся прежним;
- pending state сохраняется, чтобы клиент мог исправить его новой pending
  операцией или уничтожить поверхность;
- frame callback не активируется;
- `commit_serial` не считается принятым.

После успешного commit pending damage очищается, attach становится current, а
frame callback связывается с принятым `commit_serial`.

## Жизненный цикл поверхности

```text
surface.create
      ↓
   Unmapped ── commit(buffer) ──→ Mapped
      ↑                              │
      └──── commit(detach) ──────────┘
                    │
              surface.destroy
                    ↓
                Destroyed
```

Новая поверхность не имеет буфера и невидима. Attach без commit не отображает
её. Commit с `buffer_id = 0` снимает current buffer и переводит поверхность в
`Unmapped`.

`surface.destroy` сразу запрещает новые запросы к `SurfaceId`. Буферы,
которые ещё читает renderer или display, освобождаются позже обычным
`buffer_released`.

## Жизненный цикл буфера

```text
buffer.import → Available → Pending → InUse → Released
                    ↑                    │
                    └── buffer_released ─┘
```

- `Available`: клиент может записывать в backing storage.
- `Pending`: буфер указан в attach, но commit ещё не отправлен; compositor его
  не читает, поэтому клиент ещё может завершать отрисовку.
- `InUse`: commit принят, compositor или display может читать буфер.
- `Released`: отправлен `buffer_released`; backing storage снова доступен для
  записи и повторного attach.

`buffer.destroy` немедленно делает `BufferId` недействительным. Если буфер
ещё используется, сервис откладывает unmap и закрытие своего FD до последнего
читателя. Для уничтоженного буфера `buffer_released` не отправляется.

Клиент прекращает запись в backing storage до отправки `surface.commit` и не
возобновляет её до `buffer_released`.

## Атомарность и порядок

1. Запросы одного соединения обрабатываются в порядке кадров `ipclib`.
2. `commit_result(CORE_OK)` означает, что pending state принят, но не означает,
   что кадр уже показан.
3. `frame_done` означает, что commit или заменивший его более новый commit
   участвовал в успешно показанном output frame.
4. `buffer_released` независимо от `frame_done`; только release разрешает
   повторную запись в буфер.
5. Compositor может не показывать промежуточный commit, если до composition
   пришёл более новый. Его frame callback завершается при показе более нового
   commit той же поверхности.
6. Один `BufferId` нельзя повторно attach до `buffer_released`.

## Ошибки операций

| Операция | Возможные ошибки |
| --- | --- |
| `surface.create` | `CORE_OUT_OF_MEMORY`, `CORE_UNSUPPORTED` |
| `surface.destroy` | `CORE_NOT_FOUND`, `CORE_PERMISSION_DENIED` |
| `buffer.import` | `CORE_INVALID_ARGUMENT`, `CORE_OVERFLOW`, `CORE_UNSUPPORTED`, `CORE_OUT_OF_MEMORY`, `CORE_IO_ERROR` |
| `buffer.destroy` | `CORE_NOT_FOUND`, `CORE_PERMISSION_DENIED` |
| `surface.commit` | Сохранённая ошибка pending state, `CORE_INVALID_STATE`, `CORE_NOT_FOUND`, `CORE_OUT_OF_MEMORY`, `CORE_WOULD_BLOCK` |

Повреждённый wire format, неизвестный `message_type`, неверное число FD или
невозможный размер payload являются protocol error и закрывают соединение по
правилам базового протокола.

## Ограничения и лимиты v1

| Параметр | Предел |
| --- | --- |
| Поверхностей одного соединения | `256` |
| Импортированных буферов одного соединения | `768` |
| Размер стороны буфера | `16384` пикселей |
| Damage/opaque rects одной операции | `64` |
| Frame callbacks одной поверхности | Один pending и один committed |

Протокол v1 поддерживает только shared-memory buffers и software composition.
GPU handles, explicit fences, color spaces, HDR, subsurfaces, transforms,
fractional scale и direct scanout не определены.
