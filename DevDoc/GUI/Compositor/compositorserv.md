## Описание

compositorserv — системный сервис графической подсистемы PUNos, который
принимает поверхности приложений, импортирует shared-memory буферы, хранит
scene graph и формирует итоговый кадр для displayserv.

Сервис отделяет содержимое окна от оконной политики: приложение управляет
пикселями поверхности, windowserv — её положением и видимостью, а
compositorserv — композицией и временем жизни буферов.

## Имя сервиса

| Поле | Значение |
| --- | --- |
| Идентификатор | compositorserv |
| Исполняемый файл | compositorserv |
| Владелец запуска | initserv |
| IPC endpoint | /run/punos/compositorserv.sock |
| Версия протокола | v1 |

## Ответственность и границы

- compositorserv владеет SurfaceId, BufferId, imported FD и scene graph.
- compositorserv хранит pending и current state каждой поверхности.
- compositorserv атомарно применяет attach, damage и свойства по surface.commit.
- compositorserv планирует кадр и передаёт готовый output frame в displayserv.
- compositorserv отправляет buffer_released только после прекращения чтения.
- compositorserv применяет scene policy, полученную от windowserv, но не
  выбирает focus, z-order или geometry самостоятельно.
- compositorserv не интерпретирует ввод и не рисует widget/decorations.

## Зависимости

| Зависимость | Использование |
| --- | --- |
| sys/core | ErrorCode, GUI id, базовые типы и проверка IPC payload |
| sys/memory | Поверхности, буферы, scene graph, damage и output frames |
| sys/math | Rect, clipping, преобразование damage и пересечения |
| sys/event | Event loop IPC, displayserv и frame scheduling |
| special/ipc | Endpoint, соединения, сообщения, FD и очереди |
| displayserv | Выходы, конфигурация, present и display events |
| logserv | Запуск, клиенты, protocol errors, dropped frames и recovery |

## Жизненный цикл

### Запуск

| Параметр | Значение |
| --- | --- |
| Готовые сервисы до запуска | logserv, displayserv |
| Успех | Получен список выходов; созданы renderer, event loop и endpoint |
| displayserv без выходов | Сервис Ready, поверхности принимаются, кадры не планируются |
| Ошибка обязательной зависимости | Завершение с PROCESS_EXIT_INITIALIZATION_FAILED |

### Остановка и восстановление

При остановке сервис прекращает accept и commits, отменяет ожидающие frame
callbacks, отправляет release доступным клиентам, закрывает imported buffers,
output frames, соединения, endpoint и event loop.

При разрыве displayserv композиция приостанавливается, клиентам отправляется
surface.output_leave. После переподключения compositorserv получает новые
DisplayId и запрашивает surface.full_redraw. При потере собственного renderer
сервис завершается; initserv перезапускает его, а клиенты пересоздают объекты.

### Состояния сервиса

| Состояние | Допустимые действия | Переходы |
| --- | --- | --- |
| Starting | display connection, renderer, event loop и endpoint | Ready, Failed |
| Ready | Клиентские и scene операции, composition и present | Suspended, Stopping, Failed |
| Suspended | Surface state и reconnect displayserv; кадры не создаются | Ready, Stopping, Failed |
| Stopping | Освобождение scene, buffers и IPC | Stopped |
| Failed | Новые операции не принимаются | Завершение процесса |

### Состояния поверхности и буфера

| Объект | Состояния |
| --- | --- |
| Surface | Created, Unmapped, Mapped, Destroyed |
| Buffer | Imported, Attached, InUse, Released, Destroyed |

## Владение ресурсами

| Ресурс | Владелец | Создание | Освобождение |
| --- | --- | --- | --- |
| SurfaceId и surface state | compositorserv для клиентского соединения | surface.create | surface.destroy или disconnect |
| BufferId и imported FD | compositorserv для клиентского соединения | buffer.import | buffer.destroy, disconnect или deferred release |
| Scene binding | compositorserv по запросу windowserv | scene.bind_window | scene.remove_window или уничтожение объекта |
| Output frame | compositorserv | Перед композицией | После display present result |
| Displayserv connection | compositorserv | При запуске/reconnect | При остановке или разрыве |
| IPC server/connections | compositorserv через ipclib | При запуске и accept | При disconnect или остановке |

Обычный клиент владеет логическими объектами только своего соединения. Сервис
владеет памятью, mapping и FD их серверных представлений.

## IPC-контракт

Нормативный wire format клиентских сообщений `surface.*` и `buffer.*`
определён в [протоколе поверхностей](surface-protocol.md). Таблицы ниже дают
обзор операций сервиса; при расхождении приоритет имеет специализированный
протокол. Контракт `scene.*` будет вынесен в отдельный документ.

### Клиенты и доступ

| Клиент | Разрешённые операции | Проверка доступа |
| --- | --- | --- |
| uilib | surface.*, buffer.* и frame callbacks для собственных объектов | Владение соединения |
| windowserv | scene.* для привязанных окон и поверхностей | Доверенный scene-controller v1 |

Peer identity в ipclib отсутствует. Роль scene-controller назначается первому
доверенному соединению, выполнившему compositor.bind_controller с секретом
запуска от initserv; до появления credential API это ограничение v1.

### Формат payload

Все целые поля little-endian. Каждый запрос содержит protocol_version и
request_id. buffer.import несёт один FD; остальные сообщения FD не содержат.

| message_type | Направление | Payload |
| --- | --- | --- |
| 1, surface.create | Клиент → сервис | SurfaceCreateRequest |
| 2, surface.create_result | Сервис → клиент | SurfaceCreateResult |
| 3, surface.destroy | Клиент → сервис | SurfaceIdRequest |
| 4, surface.destroy_result | Сервис → клиент | BasicResult |
| 5, buffer.import | Клиент → сервис | BufferImportRequest + один FD |
| 6, buffer.import_result | Сервис → клиент | BufferImportResult |
| 7, buffer.destroy | Клиент → сервис | BufferIdRequest |
| 8, buffer.destroy_result | Сервис → клиент | BasicResult |
| 9, surface.attach | Клиент → сервис | SurfaceAttachRequest |
| 10, surface.damage | Клиент → сервис | SurfaceDamageRequest |
| 11, surface.set_opaque | Клиент → сервис | SurfaceRegionRequest |
| 12, surface.request_frame | Клиент → сервис | SurfaceFrameRequest |
| 13, surface.commit | Клиент → сервис | SurfaceCommitRequest |
| 14, surface.commit_result | Сервис → клиент | BasicResult |
| 15, surface.frame_done | Сервис → клиент | SurfaceFrameDoneEvent |
| 16, surface.buffer_released | Сервис → клиент | BufferReleasedEvent |
| 17, surface.output_enter | Сервис → клиент | SurfaceOutputEvent |
| 18, surface.output_leave | Сервис → клиент | SurfaceOutputEvent |
| 19, surface.full_redraw | Сервис → клиент | SurfaceRedrawEvent |
| 20, compositor.bind_controller | windowserv → сервис | ControllerBindRequest |
| 21, compositor.bind_controller_result | Сервис → windowserv | BasicResult |
| 22, scene.bind_window | windowserv → сервис | SceneBindRequest |
| 23, scene.set_geometry | windowserv → сервис | SceneGeometryRequest |
| 24, scene.set_visibility | windowserv → сервис | SceneVisibilityRequest |
| 25, scene.set_stack | windowserv → сервис | SceneStackRequest |
| 26, scene.set_output | windowserv → сервис | SceneOutputRequest |
| 27, scene.remove_window | windowserv → сервис | SceneWindowRequest |
| 28, scene.result | Сервис → windowserv | BasicResult |

### Операции

| Операция | Успешный результат | Назначение |
| --- | --- | --- |
| surface.create | Новый SurfaceId | Создаёт невидимую поверхность клиента |
| buffer.import | Новый BufferId | Проверяет descriptor и импортирует FD |
| surface.attach/damage/set_opaque | Нет немедленного ответа | Накапливает pending state до commit |
| surface.request_frame | Нет немедленного ответа | Добавляет callback к следующему показанному commit |
| surface.commit | BasicResult CORE_OK | Атомарно переносит pending state в current |
| scene.bind_window | BasicResult CORE_OK | Связывает WindowId и SurfaceId |
| scene.set_* | BasicResult CORE_OK | Изменяет scene policy одного окна |

Ошибка commit не изменяет current state. Ошибка однонаправленной pending
операции закрывает соединение только при повреждённом протоколе; предметная
ошибка сохраняется и возвращается ближайшим commit_result.

### События и подписки

| Событие | Получатель | Когда отправляется |
| --- | --- | --- |
| surface.frame_done | Владелец surface | Commit вошёл в кадр, показанный displayserv |
| surface.buffer_released | Владелец buffer | Compositor и display больше не читают buffer |
| surface.output_enter/leave | Владелец surface | Изменилось пересечение с display |
| surface.full_redraw | Владелец surface | Потеряно содержимое или изменился output pipeline |

### Типы сообщений

| Тип | Поля | Ограничения | Назначение |
| --- | --- | --- | --- |
| SurfaceCreateRequest | u16 version, u64 request_id | Версия 1 | Создание поверхности |
| SurfaceCreateResult | ErrorCode error, request_id, u64 surface_id | id определён при CORE_OK | Результат создания |
| BufferImportRequest | version, request_id, width, height, stride, pixel_format, byte_size | Один FD; XRGB/ARGB8888 | Импорт backing storage |
| BufferImportResult | ErrorCode error, request_id, u64 buffer_id | id определён при CORE_OK | Результат импорта |
| SurfaceAttachRequest | version, request_id, surface_id, buffer_id или 0 | Объекты одного клиента | Pending attach/detach |
| SurfaceDamageRequest | version, request_id, surface_id, u16 count, GuiRect[] | Не более 64 rects | Pending damage |
| SurfaceRegionRequest | version, request_id, surface_id, u16 count, GuiRect[] | Region внутри surface | Pending opaque region |
| SurfaceCommitRequest | version, request_id, surface_id, u64 commit_serial | serial возрастает для surface | Публикация pending state |
| SceneBindRequest | version, request_id, window_id, surface_id, client_connection_id | Только controller | Связь окна и surface |
| SceneGeometryRequest | version, request_id, window_id, GuiRect geometry, GuiRect clip | Валидная геометрия | Размещение в сцене |
| BasicResult | ErrorCode error, u64 request_id | CORE_OK — успех | Общий ответ |

## Контракты

1. CORE_OK является единственным успешным кодом ответа.
2. SurfaceId и BufferId уникальны в рамках запуска и принадлежат соединению.
3. Клиент не может ссылаться на объект другого соединения.
4. Pending state применяется только целиком успешным surface.commit.
5. Buffer descriptor проверяется до mmap; исполняемое отображение запрещено.
6. После commit клиент не изменяет buffer до buffer_released.
7. Удаление InUse buffer откладывает закрытие FD до последнего потребителя.
8. frame_done и buffer_released независимы; frame_done не разрешает запись.
9. Damage ограничивается surface bounds; сложный region схлопывается в bounding rect.
10. Только scene-controller меняет geometry, visibility, stack и output.
11. Disconnect удаляет все objects клиента и scene bindings без дополнительных запросов.
12. Медленный клиент отключается; composition loop не блокируется.

## Потоковая модель и асинхронность

compositorserv v1 однопоточный. Event-loop поток владеет IPC, scene graph,
surface state, mappings и display connection. Software composition выполняется
в этом же потоке только для повреждённых областей; при превышении frame budget
кадр пропускается, но очередь клиентских commits не растёт без ограничения.

## Конфигурация и постоянные данные

| Данные | Источник | Владелец | Действие при ошибке |
| --- | --- | --- | --- |
| Endpoint displayserv | Встроенная конфигурация платформы | initserv/compositorserv | Ошибка запуска или Suspended при разрыве |
| Controller secret | Одноразовый аргумент initserv | compositorserv | Без него scene-controller не назначается |
| Scene, surfaces и buffers | Только память процесса | compositorserv | Теряются при перезапуске; клиенты пересоздают |

Постоянное состояние и сохранение содержимого поверхности отсутствуют.

## Логирование и наблюдаемость

В logserv записываются запуск/остановка; connect/disconnect; protocol errors;
неверный FD/descriptor; превышение buffer/damage limits; dropped frame;
display reconnect; controller bind и renderer failure. Пиксельные данные,
заголовки окон и пользовательский ввод не журналируются.

## Ошибки

| ErrorCode | Когда возвращается клиенту |
| --- | --- |
| CORE_INVALID_ARGUMENT | Payload, geometry, region, descriptor или число FD некорректны |
| CORE_INVALID_STATE | Commit serial устарел, buffer занят недопустимо или сервис Suspended |
| CORE_PERMISSION_DENIED | Чужой объект или scene-операция обычного клиента |
| CORE_NOT_FOUND | SurfaceId, BufferId, WindowId или DisplayId отсутствует |
| CORE_OUT_OF_MEMORY | Нельзя создать object, region, mapping или output frame |
| CORE_OVERFLOW | Размер backing storage, payload или region выходит за пределы |
| CORE_WOULD_BLOCK | Очередь IPC/commit/display временно заполнена |
| CORE_IO_ERROR | Ошибка mmap, IPC, renderer или displayserv |
| CORE_UNSUPPORTED | Версия, формат или операция не поддерживается v1 |

## Ограничения версии

- Software composition, axis-aligned surfaces, XRGB8888 и ARGB8888.
- Нет GPU command buffers, effects, rotation surfaces и direct scanout.
- Нет subsurfaces, cursor plane и server-side decorations.
- Нет полноценной peer identity; controller использует временный startup secret.
- Один event-loop поток и ограниченная очередь commits на клиента.
