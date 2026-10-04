# Протокол сцены compositorserv v1

## Назначение

Протокол определяет привилегированное управление сценой между `windowserv` и
`compositorserv` через `/run/punos/compositorserv.sock`. `windowserv` задаёт
связь окна с клиентской поверхностью, её положение, видимость, порядок и
целевой дисплей. Пиксельные буферы и surface commit остаются в
[протоколе поверхностей](surface-protocol.md).

Все сообщения используют [базовый протокол GUI](../Common/protocol-base.md) и
[общие типы](../Common/common-types.md).

## Граница ответственности

```text
Приложение                    windowserv
    │                             │
    │ surface + buffer            │ window policy
    ▼                             ▼
┌─────────────────────────────────────────┐
│              compositorserv             │
│ Surface state + SceneNode + composition │
└─────────────────────────────────────────┘
```

- Приложение владеет содержимым своих буферов.
- `windowserv` владеет `WindowId` и политикой размещения.
- `compositorserv` владеет `SceneNode`, который связывает `WindowId` и
  `SurfaceId`.
- Только активный scene-controller может отправлять `scene.*`.
- Scene binding не передаёт `windowserv` владение поверхностью или буфером.

## Авторизация контроллера

`initserv` создаёт 32 случайных байта `ControllerToken` и передаёт одну копию
`compositorserv`, вторую — `windowserv` через защищённые startup arguments.
Токен не хранится на диске и не попадает в environment, IPC payload других
сервисов или журнал.

После подключения `windowserv` первым сообщением отправляет
`compositor.bind_controller`. До успешного ответа соединение не может
выполнять `scene.*`. Одновременно существует не более одного контроллера.

При отключении контроллера compositor удаляет все `SceneNode`, прекращает
показ связанных поверхностей и сохраняет сами клиентские `SurfaceId` и
`BufferId`. Новый controller повторно создаёт bindings.

Это временная модель v1. После появления peer credentials startup token должен
быть дополнен проверкой идентичности процесса.

## SurfaceBindToken

При `surface.create` compositor возвращает вместе с `SurfaceId` случайный
128-битный `SurfaceBindToken`. Клиент передаёт оба значения `windowserv` при
создании окна. `windowserv` предъявляет их в `scene.bind_window`.

Токен:

- состоит из 16 непрозрачных случайных байтов;
- действует до уничтожения поверхности или перезапуска compositor;
- не передаёт владение поверхностью;
- не журналируется;
- сравнивается за постоянное время;
- разрешает привязать поверхность только к одному живому `SceneNode`.

Это закрывает межсервисный разрыв идентичности: знание предсказуемого
`SurfaceId` без токена недостаточно для привязки чужой поверхности.

## Типы wire format

### SceneRect

Геометрия окна в глобальных логических координатах, 16 байт:

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `0` | `x` | `i32` | 24.8 fixed-point; дробная часть в v1 равна нулю |
| `4` | `y` | `i32` | 24.8 fixed-point; дробная часть в v1 равна нулю |
| `8` | `width` | `u32` | `1…16384` логических пикселей |
| `12` | `height` | `u32` | `1…16384` логических пикселей |

Compositor не масштабирует клиентский buffer: размер geometry должен совпадать
с размером current buffer поверхности. До появления current buffer geometry
сохраняется и проверяется окончательно при первом отображении.

### SceneVisibility

| Значение | Имя | Смысл |
| --- | --- | --- |
| `0` | `HIDDEN` | SceneNode не участвует в composition и hit-test |
| `1` | `VISIBLE` | SceneNode может участвовать в composition |

Occlusion вычисляет compositor и controller её не задаёт.

### SceneLayer

Слои перечислены снизу вверх:

| Значение | Имя | Назначение |
| --- | --- | --- |
| `1` | `BACKGROUND` | Обои и фон workspace |
| `2` | `NORMAL` | Обычные окна приложений |
| `3` | `TOP` | Окна always-on-top |
| `4` | `OVERLAY` | Панели, меню и системные overlays |
| `5` | `LOCK` | Экран блокировки и защищённый системный UI |

Обычные окна используют `NORMAL`. Назначение остальных слоёв определяется
политикой `windowserv`; клиент приложения слой не выбирает.

### SceneBindingLostReason

| Значение | Имя |
| --- | --- |
| `1` | `SURFACE_DESTROYED` |
| `2` | `SURFACE_CLIENT_DISCONNECTED` |

## Таблица сообщений

Номера `20–29` принадлежат протоколу scene-controller endpoint
`compositorserv`. Сообщения FD не несут.

| `message_type` | Имя | Направление | Payload |
| --- | --- | --- | --- |
| `20` | `compositor.bind_controller` | `windowserv` → compositor | `ControllerBindRequest` |
| `21` | `compositor.bind_controller_result` | compositor → `windowserv` | `BasicResult` |
| `22` | `scene.bind_window` | `windowserv` → compositor | `SceneBindRequest` |
| `23` | `scene.set_geometry` | `windowserv` → compositor | `SceneGeometryRequest` |
| `24` | `scene.set_visibility` | `windowserv` → compositor | `SceneVisibilityRequest` |
| `25` | `scene.set_stack` | `windowserv` → compositor | `SceneStackRequest` |
| `26` | `scene.set_output` | `windowserv` → compositor | `SceneOutputRequest` |
| `27` | `scene.remove_window` | `windowserv` → compositor | `SceneWindowRequest` |
| `28` | `scene.result` | compositor → `windowserv` | `BasicResult` |
| `29` | `scene.binding_lost` | compositor → `windowserv` | `SceneBindingLostEvent` |

Каждый запрос `22–27` получает один `scene.result` с тем же `request_id`.

## Схемы запросов

Все запросы начинаются с 10-байтового `RequestPrefix`:
`u16 protocol_version`, затем `u64 request_id`.

### ControllerBindRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `controller_token` | `u8[32]` |

Полный размер — 42 байта. После трёх неуспешных попыток bind соединение
закрывается. Уже назначенный controller получает `CORE_ALREADY_EXISTS`.

### SceneBindRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Ненулевой и уникальный среди SceneNode |
| `18` | `surface_id` | `u64` | Живая непривязанная поверхность |
| `26` | `surface_bind_token` | `u8[16]` | Токен этой поверхности |

Полный размер — 42 байта. Новый SceneNode создаётся в состоянии `HIDDEN`, в
слое `NORMAL`, без target display. Отдельные запросы задают остальные свойства.

### SceneGeometryRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |
| `18` | `geometry` | `SceneRect` |
| `34` | `clip_x` | `i32` |
| `38` | `clip_y` | `i32` |
| `42` | `clip_width` | `u32` |
| `46` | `clip_height` | `u32` |

Полный размер — 50 байт. Clip задаётся в пикселях поверхности. Нулевые
`clip_width` и `clip_height` вместе означают отсутствие дополнительного clip;
если ненулевые, прямоугольник ограничивается границами current buffer.

Geometry применяется атомарно. Для видимого SceneNode изменение планирует
перекомпозицию старой и новой областей.

### SceneVisibilityRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |
| `18` | `visibility` | `u32` |

Полный размер — 22 байта. Переход в `VISIBLE` разрешён только после задания
geometry и появления current buffer у связанной поверхности.

### SceneStackRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Перемещаемое окно |
| `18` | `layer` | `u32` | Одно из значений `SceneLayer` |
| `22` | `above_window_id` | `u64` | Окно того же слоя; `0` означает низ слоя |

Полный размер — 30 байт. Операция удаляет SceneNode из текущей позиции и
вставляет непосредственно над `above_window_id`. Для помещения наверх
`windowserv` указывает текущее верхнее окно слоя. Ссылка на собственный
`window_id` недопустима.

### SceneOutputRequest

| Смещение | Поле | Тип | Ограничение |
| --- | --- | --- | --- |
| `10` | `window_id` | `u64` | Существующий SceneNode |
| `18` | `display_id` | `u64` | Живой DisplayId; `0` означает automatic |

Полный размер — 26 байт. Automatic выбирает display по наибольшей площади
пересечения geometry. Явный output используется для fullscreen и системной
политики, но не масштабирует содержимое автоматически.

### SceneWindowRequest

| Смещение | Поле | Тип |
| --- | --- | --- |
| `10` | `window_id` | `u64` |

Полный размер — 18 байт.

## Ответы

`compositor.bind_controller_result` и `scene.result` используют 12-байтовый
`BasicResult`: `u32 error`, затем `u64 request_id`. `CORE_OK` является
единственным успешным значением.

После успешного result изменение уже находится в scene graph. Result не
означает, что новый output frame показан физически.

## SceneBindingLostEvent

| Смещение | Поле | Тип |
| --- | --- | --- |
| `0` | `serial` | `u64` |
| `8` | `window_id` | `u64` |
| `16` | `surface_id` | `u64` |
| `24` | `reason` | `u32` |

Полный размер — 28 байт. Перед отправкой события SceneNode уже удалён и не
участвует в composition. `windowserv` переводит окно в `Unmapped` и уведомляет
клиента о приостановке rendering или закрывает окно согласно своей политике.

## Жизненный цикл SceneNode

```text
scene.bind_window
        ↓
      Hidden ── set_visibility(VISIBLE) ──→ Visible
        ↑                                      │
        └──── set_visibility(HIDDEN) ──────────┘
                         │
              scene.remove_window
              или binding_lost
                         ↓
                      Removed
```

Geometry, stack и output можно менять в `Hidden` и `Visible`. Новый SceneNode
сначала полностью настраивается скрытым и только последним запросом становится
видимым. Это предотвращает показ окна с промежуточными свойствами.

`scene.remove_window` удаляет только SceneNode. Клиентские `SurfaceId`,
`BufferId` и их текущее содержимое остаются живыми.

## Инварианты сцены

1. Одному `WindowId` соответствует не более одного SceneNode.
2. Одна поверхность одновременно связана не более чем с одним SceneNode.
3. `windowserv` не может bind поверхность без правильного `SurfaceBindToken`.
4. Только controller меняет geometry, visibility, stack и output.
5. Hidden SceneNode не участвует в composition и hit-test.
6. Порядок SceneNode однозначен: сначала `SceneLayer`, затем связный порядок
   внутри слоя.
7. `LOCK` всегда выше остальных слоёв; `BACKGROUND` всегда ниже.
8. Compositor не меняет оконную политику самостоятельно.
9. Удаление или disconnect владельца поверхности автоматически удаляет
   связанный SceneNode и создаёт `scene.binding_lost`.
10. Ошибка операции не изменяет scene graph.

## Порядок и восстановление

- Сообщения controller обрабатываются последовательно в порядке `ipclib`.
- Для создания окна controller выполняет `bind_window`, `set_geometry`,
  `set_stack`, `set_output`, затем `set_visibility(VISIBLE)`.
- При потере display явный target сбрасывается в automatic, compositor
  пересчитывает пересечение и отправляет surface output events клиенту.
- При перезапуске compositor все SceneNode, SurfaceId и токены теряются;
  приложения пересоздают поверхности, после чего `windowserv` создаёт bindings.
- При перезапуске `windowserv` compositor удаляет старую сцену после disconnect;
  приложения повторно создают окна и передают действующие surface token новому
  экземпляру `windowserv`.

## Ошибки

| Ситуация | `ErrorCode` |
| --- | --- |
| Неверный enum, geometry, clip или payload | `CORE_INVALID_ARGUMENT` |
| Неподдерживаемая версия | `CORE_UNSUPPORTED` |
| Неверный ControllerToken или SurfaceBindToken | `CORE_PERMISSION_DENIED` |
| Controller уже назначен или WindowId уже существует | `CORE_ALREADY_EXISTS` |
| WindowId, SurfaceId, anchor или DisplayId отсутствует | `CORE_NOT_FOUND` |
| Surface уже привязана или show выполняется без buffer/geometry | `CORE_INVALID_STATE` |
| Нельзя создать SceneNode или damage region | `CORE_OUT_OF_MEMORY` |
| Очередь compositor/display временно заполнена | `CORE_WOULD_BLOCK` |

Повреждённый wire format и `scene.*` до успешного controller bind являются
protocol error и закрывают соединение.

## Ограничения v1

- Один scene-controller и один workspace.
- Нет атомарной транзакции для нескольких SceneNode. Новое окно настраивается
  скрытым; перестановка нескольких уже видимых окон может попасть в разные
  output frames.
- Нет fractional positioning, surface transform и compositor-side scaling.
- Нет нескольких seats, virtual desktops, animations и effects.
- Авторизация использует startup token до появления peer credentials.
