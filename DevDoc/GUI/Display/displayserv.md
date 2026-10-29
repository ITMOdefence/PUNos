## Описание

displayserv — системный сервис графической подсистемы PUNos, который
предоставляет compositorserv доступ к физическим дисплеям. Сервис перечисляет
выходы и режимы, применяет конфигурацию, принимает готовый кадр и передаёт его
displaylib для показа.

displayserv не знает об окнах, поверхностях, фокусе и приложениях. Его граница
заканчивается на одном готовом кадре выбранного физического выхода.

## Имя сервиса

| Поле | Значение |
| --- | --- |
| Идентификатор | displayserv |
| Исполняемый файл | displayserv |
| Владелец запуска | initserv |
| IPC endpoint | /run/punos/displayserv.sock |
| Версия протокола | v1 |

## Ответственность и границы

- displayserv единолично владеет DisplayBackendHandle и состояниями выходов.
- displayserv назначает DisplayId, уникальный в рамках одного запуска сервиса.
- displayserv выбирает допустимый режим по запросу compositorserv и хранит
  generation текущей конфигурации каждого выхода.
- displayserv проверяет descriptor и полученный FD до импорта кадра.
- displayserv сериализует configure и present для одного выхода.
- displayserv не выполняет композицию, не хранит SurfaceId или WindowId и не
  читает устройства ввода.
- displayserv не определяет расположение дисплеев в глобальном пространстве:
  эту политику хранит compositorserv/windowserv.

## Зависимости

| Зависимость | Использование |
| --- | --- |
| sys/core | ErrorCode, DisplayId, базовые типы и проверка IPC payload |
| sys/memory | Таблица выходов, клиенты, запросы present и сериализация ответов |
| sys/string | Имена backend/выходов и IPC endpoint |
| sys/event | Один event loop для backend, endpoint и соединений |
| sys/time | MonotonicTime завершения present |
| special/display | Открытие backend, режимы, импорт кадров и present |
| special/ipc | Endpoint, соединения, сообщения, FD и исходящие очереди |
| logserv | Запуск, hotplug, configure, present failures и восстановление |

Для рабочего v1 ipclib должен поддерживать ограниченную передачу одного FD в
кадре IPC. Без этой возможности допустим только тестовый backend; копирование
полноразмерного кадра в payload не является публичным контрактом.

## Жизненный цикл

### Запуск

| Параметр | Значение |
| --- | --- |
| Готовые сервисы до запуска | logserv |
| Параметры | `--backend=<name>`, необязательный `--device=<path>` и PresenterToken от initserv |
| Успех | Открыт displaylib backend; созданы event loop и endpoint; выходы перечислены |
| Нет подключённых выходов | Сервис переходит в Ready и ожидает hotplug |
| Ошибка backend, event loop или endpoint | Завершение с PROCESS_EXIT_INITIALIZATION_FAILED |

Endpoint создаётся только после успешной инициализации backend и event loop.

### Остановка и восстановление

При остановке сервис прекращает accept и отклоняет новые configure/present.
Ожидающие present завершаются display.present_failed, imported frames
освобождаются, соединения закрываются, затем закрываются backend и endpoint.

При BackendLost сервис отправляет display.backend_lost всем клиентам и
завершается. initserv может перезапустить его; все DisplayId и generation после
перезапуска новые. compositorserv обязан переподключиться, заново получить
список выходов и сформировать полный кадр.

### Состояния сервиса

| Состояние | Допустимые действия | Переходы |
| --- | --- | --- |
| Starting | Backend, event loop, endpoint и список выходов | Ready, Failed |
| Ready | list, get_state, configure, present и backend events | Reconfiguring, Stopping, Failed |
| Reconfiguring | Только события backend и завершение configure | Ready, Failed |
| Stopping | Отмена present и освобождение ресурсов | Stopped |
| Failed | Новые операции не принимаются | Завершение процесса |

## Владение ресурсами

| Ресурс | Владелец | Создание | Освобождение |
| --- | --- | --- | --- |
| DisplayBackendHandle | displayserv через displaylib | При запуске | При остановке или BackendLost |
| DisplayId и состояние выхода | displayserv | При перечислении или OutputAdded | При OutputRemoved или остановке |
| DisplayFrameHandle | displayserv | После успешного импорта FD | После presented/present_failed или ошибки принятия |
| Полученная копия FD кадра | DisplayFrameHandle | При display_frame_import | Только display_frame_destroy |
| IpcServerHandle и соединения | displayserv через ipclib | При запуске и accept | При отключении или остановке |
| EventLoopHandle | displayserv | При запуске | При остановке |

FD backend и IPC не закрываются напрямую: они принадлежат создавшим их handle.

## IPC-контракт

Нормативный wire format операций, результатов и событий определён в
[протоколе вывода](presentation-protocol.md). Таблицы ниже дают обзор сервиса;
при расхождении приоритет имеет специализированный протокол.

### Клиенты и доступ

| Клиент | Разрешённые операции | Проверка доступа |
| --- | --- | --- |
| compositorserv | display.list, display.get_state, display.configure, display.present, display.subscribe | Успешный display.bind_presenter с PresenterToken |
| Диагностика GUI | display.list, display.get_state, display.subscribe | Права Unix endpoint; только read-only операции |

В ipclib v1 нет достоверной peer identity. Endpoint защищается правами Unix
socket; изменяющие операции дополнительно требуют одноразовый PresenterToken,
переданный initserv обоим GUI-сервисам.

### Формат payload

Каждое сообщение — один кадр ipclib. Целые поля little-endian, строки UTF-8
без NUL. Внутренние handle и указатели не передаются. display.present несёт
ровно один ancillary FD; остальные сообщения не несут FD.

| message_type | Направление | Payload |
| --- | --- | --- |
| 1, display.list | Клиент → сервис | VersionedRequest |
| 2, display.list_result | Сервис → клиент | DisplayListResult |
| 3, display.get_state | Клиент → сервис | DisplayIdRequest |
| 4, display.get_state_result | Сервис → клиент | DisplayStateResult |
| 5, display.configure | Клиент → сервис | DisplayConfigureRequest |
| 6, display.configure_result | Сервис → клиент | DisplayConfigureResult |
| 7, display.present | Клиент → сервис | DisplayPresentRequest + один FD |
| 8, display.present_result | Сервис → клиент | DisplayPresentResult |
| 9, display.subscribe | Клиент → сервис | VersionedRequest |
| 10, display.subscribe_result | Сервис → клиент | DisplaySubscribeResult |
| 11, display.added | Сервис → подписчик | DisplayInfoEvent |
| 12, display.removed | Сервис → подписчик | DisplayRemovedEvent |
| 13, display.changed | Сервис → подписчик | DisplayInfoEvent |
| 14, display.presented | Сервис → инициатор | DisplayPresentedEvent |
| 15, display.present_failed | Сервис → инициатор | DisplayPresentFailedEvent |
| 16, display.backend_lost | Сервис → подписчик | DisplayBackendLostEvent |
| 17, display.bind_presenter | compositorserv → сервис | PresenterBindRequest |
| 18, display.bind_presenter_result | Сервис → compositorserv | BasicResult |

### Операции

| Операция | Запрос | Успешный ответ | Назначение |
| --- | --- | --- | --- |
| display.list | VersionedRequest | DisplayListResult | Возвращает снимок подключённых выходов |
| display.get_state | DisplayIdRequest | DisplayStateResult | Возвращает состояние одного выхода |
| display.configure | DisplayConfigureRequest | DisplayConfigureResult | Применяет режим, orientation и enabled |
| display.present | DisplayPresentRequest + FD | DisplayPresentResult | Импортирует и ставит кадр в очередь |
| display.subscribe | VersionedRequest | DisplaySubscribeResult | Создаёт подписку соединения на output/backend events |
| display.bind_presenter | PresenterBindRequest | BasicResult | Назначает единственный привилегированный compositorserv |

Успешный DisplayPresentResult означает принятие кадра, но не показ. Итог
приходит отдельным display.presented или display.present_failed.

### События и подписки

| Событие | Получатели | Данные | Когда отправляется |
| --- | --- | --- | --- |
| display.added | Подписчики | Полный DisplayInfo | Новый выход доступен |
| display.removed | Подписчики | DisplayId и generation | Выход удалён |
| display.changed | Подписчики | Полный DisplayInfo | Свойства или режим изменились |
| display.presented | Инициатор present | request_id, DisplayId, timestamp | Backend завершил показ |
| display.present_failed | Инициатор present | request_id, DisplayId, ErrorCode | Backend отклонил или потерял кадр |
| display.backend_lost | Подписчики | ErrorCode | Backend больше не обслуживается |

### Типы сообщений

| Тип | Поля | Ограничения | Назначение |
| --- | --- | --- | --- |
| VersionedRequest | u16 protocol_version, u64 request_id | Версия 1; request_id ненулевой | Общий запрос без предметных полей |
| DisplayIdRequest | u16 protocol_version, u64 request_id, u64 display_id | display_id ненулевой | Выбор выхода |
| DisplayMode | u32 width_px, u32 height_px, u32 refresh_millihz | Все значения ненулевые | Режим выхода |
| DisplayInfo | u64 display_id, u64 generation, u8 connected, u8 enabled, физический размер, current_mode, u16 mode_count, DisplayMode[] | mode_count не более 128 | Снимок выхода |
| DisplayListResult | ErrorCode error, request_id, snapshot_serial, u16 count, DisplayInfo[] | count не более 32; список определён при CORE_OK | Список выходов |
| DisplayStateResult | ErrorCode error, request_id, DisplayInfo info | info определён при CORE_OK | Состояние выхода |
| DisplayConfigureRequest | version, request_id, display_id, generation, enabled, orientation, DisplayMode | generation должен совпадать | Новая конфигурация |
| DisplayConfigureResult | ErrorCode error, u64 request_id, DisplayInfo applied | applied определён при CORE_OK | Фактическая конфигурация |
| DisplayPresentRequest | version, request_id, display_id, generation, width, height, stride, pixel_format, byte_size, u16 damage_count, GuiRect[] | Один FD; не более 64 rects | Метаданные готового кадра |
| DisplayPresentResult | ErrorCode error, u64 request_id | CORE_OK означает постановку в очередь | Приём present |
| DisplayPresentedEvent | request_id, display_id, u64 monotonic_ms | Только для принятого кадра | Завершение показа |
| DisplayPresentFailedEvent | request_id, display_id, ErrorCode error | error не CORE_OK | Асинхронный отказ |

## Контракты

1. CORE_OK является единственным успешным ErrorCode ответа.
2. DisplayId уникален до OutputRemoved или перезапуска displayserv.
3. Любое изменение выхода увеличивает generation; запрос со старым generation
   возвращает CORE_INVALID_STATE.
4. Для одного выхода одновременно допускается один frame в backend и один в
   очереди displayserv.
5. Полученный FD принимается во владение только после успешного импорта. При
   ошибке кадра ipclib/displayserv закрывает непотреблённую копию ровно один раз.
6. Размер backing storage не меньше stride * height; вычисление проверяется на
   переполнение до mmap.
7. После configure compositorserv обязан выполнить полный redraw.
8. OutputRemoved завершает все его ожидающие present через
   display.present_failed с CORE_NOT_FOUND.
9. Медленный IPC-клиент отключается; backend event loop не блокируется.
10. displayserv не сохраняет FD или кадр после финального события present.

## Потоковая модель и асинхронность

displayserv v1 однопоточный. Один поток владеет event loop, backend, выходами,
соединениями и очередями. При готовности backend события читаются до NoEvent;
IPC accept/read/flush выполняются до CORE_WOULD_BLOCK. Блокирующие операции и
worker threads отсутствуют.

## Конфигурация и постоянные данные

| Данные | Источник | Владелец | Действие при ошибке |
| --- | --- | --- | --- |
| Имя backend и путь устройства | Аргументы initserv | initserv | Ошибка запуска displayserv |
| PresenterToken | Одноразовые startup arguments initserv | displayserv до успешного bind | Privileged клиент не назначается |
| Режим и enabled выхода | Только память процесса | displayserv | После перезапуска выбирается безопасный режим backend |
| DisplayId и generation | Только память процесса | displayserv | Создаются заново после перезапуска |

Постоянное хранение layout и режима дисплеев в v1 отсутствует.

## Логирование и наблюдаемость

В logserv записываются запуск и остановка; backend и device; OutputAdded,
OutputRemoved и configure; ошибка импорта FD; отклонённый present; backend
failure; переполнение очереди и отключение медленного клиента. Пиксельные
данные, содержимое кадра и пользовательские изображения не журналируются.

## Ошибки

| ErrorCode | Когда возвращается клиенту |
| --- | --- |
| CORE_INVALID_ARGUMENT | Неверный payload, размер, stride, orientation, format или число FD |
| CORE_INVALID_STATE | Устаревший generation, выключенный/reconfiguring выход или заполненная очередь |
| CORE_PERMISSION_DENIED | Клиенту не разрешена изменяющая операция |
| CORE_NOT_FOUND | DisplayId отсутствует или выход удалён |
| CORE_OUT_OF_MEMORY | Нельзя создать состояние, ответ или импорт frame |
| CORE_OVERFLOW | stride * height или полный payload выходит за диапазон |
| CORE_WOULD_BLOCK | Очередь present или IPC временно заполнена |
| CORE_IO_ERROR | Ошибка backend, mmap, IPC или present |
| CORE_UNSUPPORTED | Версия, режим, orientation, формат или backend не поддерживается |

## Ограничения версии

- Только XRGB8888 и premultiplied ARGB8888.
- Нет HDR, color management, variable refresh rate и hardware overlays.
- Нет постоянной конфигурации дисплеев и глобального display layout.
- Нет peer identity; presenter использует временный startup token.
- Возможности hotplug зависят от выбранного displaylib backend.
