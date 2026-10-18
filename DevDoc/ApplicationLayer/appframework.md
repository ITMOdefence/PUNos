# appframework

## Описание

`appframework` — стандартная инфраструктурная библиотека нативных
пользовательских приложений PUNos. Она создаёт контекст одного приложения,
выполняет его главный event loop, даёт доступ к базовым системным сервисам и
обеспечивает штатное завершение.

Библиотека не является сервисом и не создаёт отдельный процесс. Её код
загружается в адресное пространство каждого использующего её приложения.

## Имя и физическая форма

| Поле | Значение |
| --- | --- |
| Имя библиотеки | `appframework` |
| Публичный заголовок | `appframework/app.h` |
| Форма поставки v1 | Статическая библиотека `libappframework.a` |
| Получатели | Графическая оболочка и нативные приложения |
| IPC endpoint | Отсутствует |

## Ответственность и границы

- Создаёт и уничтожает `AppHandle`; владеет главным event loop приложения.
- Даёт `uilib` и приложению возможность регистрировать источники событий.
- Является неблокирующим IPC-клиентом `logserv` и `appserv`.
- Устанавливает обработчик `SIGTERM`, который запрашивает штатное завершение.
- Не создаёт процессы, не хранит `ProcessHandle` и не запускает ELF: это делает `appserv`.
- Не создаёт окна, виджеты и не рисует интерфейс: это делает `uilib`.
- Не читает физические устройства и не взаимодействует с `IOserv` напрямую.
- Не содержит поддержку конкретного языка программирования, runtime или VM.

## Внутренние модули

| Модуль | Ответственность |
| --- | --- |
| `application` | `AppHandle`, конфигурация и состояния приложения |
| `runtime` | Создание event loop, подключение к сервисам и `SIGTERM` |
| `eventloop` | Источники FD и таймеры, ожидание и вызов callback |
| `applications` | Асинхронный клиент `appserv` |
| `logging` | Неблокирующий клиент `logserv` |
| `lifecycle` | Остановка event loop и освобождение ресурсов |

Модули являются частями одной библиотеки, а не самостоятельными публичными
библиотеками.

## Зависимости

| Зависимость | Использование |
| --- | --- |
| `sys/core` | `ErrorCode`, базовые типы, `Bytes` и проверки данных |
| `sys/memory` | `AppHandle`, очередь запросов и копии ответов |
| `sys/string` | `application_id`, сообщения журнала и IPC endpoint |
| `sys/eventloop` | Event loop, IPC-дескрипторы и таймеры |
| `sys/time` | `DurationMs` для таймеров |
| Syscall Wrappers | `sigaction()` для `SIGTERM`; `getpid()` и `gettid()` для записей журнала и проверки потока-владельца |
| `special/ipc` | Неблокирующие клиентские IPC-соединения |
| `special/log` | Только константы и значения `LogSeverity` для совместимости с протоколом `logserv`; линковка не требуется |
| `logserv` | Записи `app_log()` через `/run/punos/logserv.sock` |
| `appserv` | Каталог и жизненный цикл приложений через `/run/punos/appserv.sock` |
| `uilib` | Не является зависимостью: `uilib` использует `appframework` |

## Интеграция с приложением

CRT передаёт управление функции `main()`. Нативное приложение создаёт
`AppHandle` (в процессе одновременно существует не более одного), настраивает собственный код и `uilib`, запускает `app_run()`,
после чего уничтожает контекст и возвращается из `main()`.

```text
main()
  → app_create()
  → настройка приложения и uilib
  → app_run()
  → app_destroy()
  → возврат из main()
```

`uilib` создаёт окна, виджеты и отрисовку. Для IPC-дескрипторов графических
сервисов она регистрирует собственные источники через API `appframework`.
Сам `appframework` не содержит типа `Window` и не декодирует UI-события.

## Связь с appserv

`appframework` не запускает программы сам. Модуль `applications` сериализует
запрос протокола `appserv`, отправляет его неблокирующе и вызывает callback в
потоке-владельце `AppHandle` после получения ответа.

| Операция appframework | Операция appserv | Результат callback |
| --- | --- | --- |
| `app_apps_list()` | `app.list` | Список `AppInfo` |
| `app_apps_get_info()` | `app.get_info` | Один `AppInfo` |
| `app_apps_launch()` | `app.launch` | Новый `AppInstanceId` |
| `app_apps_stop()` | `app.stop` | Результат отправки `SIGTERM` |
| `app_apps_get_status()` | `app.get_status` | Состояние и код завершения |

Соединение с `appserv` создаётся лениво при первом запросе. Успешный возврат
из `app_apps_*()` означает только постановку запроса. Результат операции
`appserv`, включая `ErrorCode`, приходит в callback. Формат сообщений,
сопоставление ответов и поведение при отказе соединения описаны в разделе
«Протокол клиента appserv».

В `ipclib` v1 нет идентичности IPC-клиента. Поэтому особая роль графической
оболочки пока является соглашением доверенных компонентов: `appserv` не может
технически отличить её от другого процесса с доступом к своему endpoint.

## Типы

Значения перечислений в C получают имя типа в качестве префикса
(`<TYPE_NAME>_<VALUE>`), как принято в `System/SystemLibs/README.md`;
в таблицах ниже указаны короткие имена. Числовые значения входят в ABI.

| Тип | Представление | Владение | Назначение |
| --- | --- | --- | --- |
| `AppHandle` | `struct AppHandle *` | Создаёт `app_create()`, уничтожает `app_destroy()` | Контекст одного приложения |
| `AppConfig` | Структура: `const char *application_id`, `const char *log_component` | Строки копируются при `app_create()` и нужны только на время вызова | Начальная конфигурация |
| `AppEventSourceId` | Ненулевой `u64` | До `app_event_remove()` или `app_destroy()` | Идентификатор источника event loop; уникален, пока источник зарегистрирован |
| `AppEventHandler` | `void (*)(AppHandle app, AppEventSourceId source, EventMask mask, void *context)` | Не владеет данными события | Обработчик FD или таймера; `EventMask` — тип `sys/eventloop` |
| `AppLogLevel` | Перечисление: `Trace` = 0, `Debug` = 1, `Info` = 2, `Warning` = 3, `Error` = 4, `Fatal` = 5 | Не требует освобождения | Уровень записи; значения равны `LogSeverity` из `special/log` |
| `AppAppsOperation` | Перечисление: `List` = 0, `GetInfo` = 1, `Launch` = 2, `Stop` = 3, `GetStatus` = 4 | Не требует освобождения | Операция, ответ на которую пришёл в callback |
| `AppInstanceState` | Перечисление: `Starting` = 0, `Running` = 1, `Terminating` = 2, `Exited` = 3 | Не требует освобождения | Состояние экземпляра; значения равны проводным значениям `appserv` |
| `AppExitKind` | Перечисление: `Exited` = 0, `Signaled` = 1 | Не требует освобождения | Вид завершения; значения равны `ProcessTerminationKind` |
| `AppInfo` | Структура: `const char *application_id`, `const char *display_name` | Принадлежит framework на время callback | Элемент каталога; обе строки — UTF-8 с завершающим `NUL` |
| `AppInstanceId` | Ненулевой `u64` | Не является PID | Идентификатор экземпляра от `appserv` |
| `AppInstanceStatus` | Структура: `AppInstanceState state`, `AppExitKind exit_kind`, `i32 exit_value` | Копируется в `AppAppsResult` | Состояние экземпляра; `exit_kind` и `exit_value` определены только для `Exited`, иначе равны `0` |
| `AppAppsResult` | Структура: `AppAppsOperation operation` и объединение: `list` (`const AppInfo *items`, `usize count`), `info` (`AppInfo`), `instance` (`AppInstanceId`), `status` (`AppInstanceStatus`) | Действителен только во время callback | Успешный результат; для `Stop` полезной нагрузки нет |
| `AppAppsCallback` | `void (*)(AppHandle app, AppRequestId request, ErrorCode error, const AppAppsResult *result, void *context)` | Ответ действителен только во время callback | Обработчик асинхронного ответа; `result` равен `NULL`, если `error` не `CORE_OK` |
| `AppRequestId` | Ненулевой `u64`, растёт на 1 с каждым запросом, начиная с 1 | До callback | Связывает запрос и ответ `appserv`; уникален за время жизни `AppHandle` |

`application_id` приложения — 1…64 байта UTF-8; `log_component` — 1…256 байтов
UTF-8 (`LOG_COMPONENT_MAX_LENGTH`). Поля `application_id` и `display_name` в
`AppInfo` — UTF-8 без `NUL` длиной не более 64 байтов в протоколе: это
ограничение IPC-контракта `appserv`. Framework добавляет завершающий `NUL`
в своей копии.

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `APP_ID_MAX_LENGTH` | `64` | Максимальная длина `application_id` и `display_name` в байтах |
| `APP_LOG_MESSAGE_MAX_LENGTH` | `16384` | Максимальная длина сообщения `app_log()`; равна `LOG_MESSAGE_MAX_LENGTH` |
| `APP_LOG_COMPONENT_MAX_LENGTH` | `256` | Максимальная длина `log_component`; равна `LOG_COMPONENT_MAX_LENGTH` |
| `APP_APPS_MAX_PENDING` | `256` | Максимальное число запросов к `appserv`, ожидающих ответа |
| `APP_LOG_MAX_PENDING` | `256` | Максимальное число записей журнала, ожидающих отправки или подтверждения |
| `APP_PROTOCOL_VERSION` | `1` | Версия протоколов `appserv` и `logserv`, которую отправляет framework |

## Публичные операции

| Операция | Возвращаемое значение | Назначение |
| --- | --- | --- |
| `app_create(const AppConfig *config, AppHandle *out_app)` | `ErrorCode` | Проверяет конфигурацию, создаёт контекст, event loop и обработчик `SIGTERM`; запоминает вызывающий поток как поток-владельца |
| `app_run(AppHandle app)` | `ErrorCode` | Выполняет event loop до запроса выхода (`CORE_OK`) или неустранимой ошибки event loop (код ошибки) |
| `app_request_quit(AppHandle app)` | `ErrorCode` | Запрашивает штатный выход из `app_run()`; допустим из обработчика сигнала и из любого потока |
| `app_destroy(AppHandle *app)` | `ErrorCode` | Завершает висящие запросы, закрывает IPC-клиенты, освобождает контекст и записывает `NULL` |
| `app_event_add_fd(AppHandle app, i32 fd, EventMask mask, AppEventHandler handler, void *context, AppEventSourceId *out_source)` | `ErrorCode` | Регистрирует невладеющий FD; `mask` — непустая комбинация `EVENT_READABLE` и `EVENT_WRITABLE` |
| `app_event_create_timer(AppHandle app, DurationMs delay_ms, DurationMs period_ms, AppEventHandler handler, void *context, AppEventSourceId *out_source)` | `ErrorCode` | Регистрирует таймер; `period_ms = 0` — одноразовый, `delay_ms = 0` — срабатывание при ближайшем ожидании |
| `app_event_remove(AppHandle app, AppEventSourceId source)` | `ErrorCode` | Удаляет источник; внешний FD не закрывается |
| `app_log(AppHandle app, AppLogLevel level, const char *message)` | `ErrorCode` | Неблокирующе ставит запись в очередь `logserv`; `message` — UTF-8 с завершающим `NUL` |
| `app_apps_list(AppHandle app, AppAppsCallback callback, void *context, AppRequestId *out_request)` | `ErrorCode` | Запрашивает каталог у `appserv` |
| `app_apps_get_info(AppHandle app, const char *application_id, AppAppsCallback callback, void *context, AppRequestId *out_request)` | `ErrorCode` | Запрашивает метаданные приложения |
| `app_apps_launch(AppHandle app, const char *application_id, AppAppsCallback callback, void *context, AppRequestId *out_request)` | `ErrorCode` | Просит `appserv` запустить приложение |
| `app_apps_stop(AppHandle app, AppInstanceId instance, AppAppsCallback callback, void *context, AppRequestId *out_request)` | `ErrorCode` | Просит `appserv` отправить SIGTERM |
| `app_apps_get_status(AppHandle app, AppInstanceId instance, AppAppsCallback callback, void *context, AppRequestId *out_request)` | `ErrorCode` | Запрашивает состояние экземпляра |

Параметр `out_request` необязателен: при `NULL` идентификатор не возвращается.
Остальные `out_*` определены только при `CORE_OK`. `callback` не может быть `NULL`.

## Модель событий

Один поток-владелец `AppHandle` ожидает события через `sys/eventloop` и
последовательно вызывает обработчики.

| Источник | Кто регистрирует | Действие framework |
| --- | --- | --- |
| IPC `uilib` с графическими сервисами | `uilib` | Вызывает обработчик `uilib` |
| IPC `appserv` | `appframework` | Разбирает ответ и вызывает `AppAppsCallback` |
| IPC `logserv` | `appframework` | Передаёт очередь записей, принимает подтверждения |
| Таймер | Приложение или `uilib` | Вызывает зарегистрированный обработчик с маской `EVENT_READABLE` |
| `SIGTERM` | `appframework` | Запрашивает выход и пробуждает event loop |

Правила диспетчеризации:

- Событие внутреннего источника пробуждения (`Wake` в `sys/eventloop`) не
  передаётся никакому обработчику: оно только прерывает ожидание.
- События одной пачки обрабатываются по порядку. Если обработчик удалил
  источник (в том числе собственный), его события из той же пачки не
  доставляются. Событие привязано к идентификатору источника, а не к FD, поэтому
  новый источник на том же FD не получает событие старого.
- Источник, зарегистрированный из обработчика, получает события не раньше
  следующего ожидания.
- После запроса выхода framework дожидается возврата текущего обработчика,
  остальные события пачки не доставляет и выходит из `app_run()`.
- `CORE_INTERRUPTED` от ожидания (прерывание сигналом) не является ошибкой:
  цикл продолжается.
- Callback `AppAppsCallback` вызываются только из `app_run()`, кроме
  завершения висящих запросов в `app_destroy()`. Callback никогда не
  вызывается до возврата `CORE_OK` из `app_apps_*()`.

## Жизненный цикл и владение ресурсами

| Состояние | Допустимые действия | Переходы |
| --- | --- | --- |
| `Created` | Настройка `uilib`, обработчиков, запросы и записи журнала | `Running`, `Stopping` |
| `Running` | События и асинхронные запросы | `Stopping`, `Failed` |
| `Stopping` | Удаление источников и `app_destroy()` | `Destroyed` |
| `Failed` | Удаление источников и `app_destroy()`; новые запросы не принимаются | `Stopping` |
| `Destroyed` | Нет; указатель обнулён | Нет |

Переходы:

- `Created → Running` выполняет `app_run()`. Если выход уже запрошен
  (`app_request_quit()` или `SIGTERM` до `app_run()`), `app_run()` сразу
  переходит в `Stopping` и возвращает `CORE_OK`, не вызывая обработчиков.
- `Running → Stopping`: запрошен выход. `Running → Failed`: ожидание event loop
  вернуло ошибку, отличную от `CORE_INTERRUPTED`; `app_run()` возвращает эту
  ошибку (неизвестный код заменяется на `CORE_IO_ERROR`) и записывает
  диагностику в `logserv`. Отказ IPC-соединения с сервисом не переводит
  в `Failed`.
- `Created → Stopping` и `Failed → Stopping` выполняет `app_destroy()`.
- Повторный запуск `app_run()` после выхода не поддерживается.

Допустимость операций по состояниям (`OK` — штатное выполнение, `STATE` —
`CORE_INVALID_STATE`):

| Операция | `Created` | `Running` | `Stopping` | `Failed` |
| --- | --- | --- | --- | --- |
| `app_run` | OK | STATE | STATE | STATE |
| `app_request_quit` | OK | OK | OK | OK |
| `app_destroy` | OK | STATE | OK | OK |
| `app_event_add_fd`, `app_event_create_timer` | OK | OK | STATE | STATE |
| `app_event_remove` | OK | OK | OK | OK |
| `app_log` | OK | OK | STATE | STATE |
| `app_apps_*` | OK | OK | STATE | STATE |

Вызов из потока, не являющегося владельцем, возвращает `CORE_INVALID_STATE`
для всех операций, кроме `app_request_quit()`.

| Ресурс | Владелец | Освобождение |
| --- | --- | --- |
| `AppHandle` и event loop | `appframework` | `app_destroy()` |
| Клиенты `logserv` и `appserv` | `appframework` | `app_destroy()` или отказ соединения |
| Очередь запросов и копии ответов | `appframework` | Callback или `app_destroy()` |
| Внешний FD | Вызывающий код или `uilib` | Владелец закрывает его после `app_event_remove()` |
| Окна и виджеты | `uilib` | По контракту `uilib`, до `app_destroy()` |

После `SIGTERM` framework завершает `app_run()`. Приложение обязано вызвать
`app_destroy()` и вернуться из `main()`, выполняя требование `appserv` v1.

Порядок `app_destroy()`:

1. Состояние становится `Stopping`.
2. Каждый висящий запрос к `appserv` получает callback с `CORE_INVALID_STATE`,
   в порядке постановки. Состояние при этом `Stopping`: из такого callback
   операции подчиняются таблице выше, а повторный `app_destroy()` возвращает
   `CORE_INVALID_STATE`.
3. Делается одна неблокирующая попытка передать накопленные записи `logserv`,
   включая запись об уничтожении; доставка не гарантируется.
4. Закрываются соединения, удаляются источники и event loop, восстанавливается
   прежний обработчик `SIGTERM`, память освобождается, `*app` получает `NULL`.

## Протокол клиента appserv

Framework всегда отправляет `protocol_version = 1`. Сообщения и их
размеры определены в `DevDoc/SystemServices/base/appserv.md`; клиент строит их
так:

| Операция | `message_type` | Payload запроса |
| --- | --- | --- |
| `app_apps_list()` | 1 | Пустой |
| `app_apps_get_info()` | 3 | `u16` версия, `u16` длина id, байты id |
| `app_apps_launch()` | 5 | `u16` версия, `u16` длина id, байты id |
| `app_apps_stop()` | 7 | `u16` версия, `u64` идентификатор экземпляра |
| `app_apps_get_status()` | 9 | `u16` версия, `u64` идентификатор экземпляра |

Сопоставление ответов и запросов:

- В протоколе нет идентификатора запроса, поэтому `appserv` отвечает строго в
  порядке получения, а framework ведёт очередь FIFO запросов, отправленных или
  ожидающих отправки. `AppRequestId` существует только внутри процесса.
- Ответ для первого запроса очереди должен иметь тип `запрос + 1`.
- Ответ с успехом проверяется строго: точный размер, не более 64
  приложений в каталоге, каждая строка не длиннее 64 байтов, непустой id,
  корректный UTF-8, ненулевой `instance_id`, состояние `Running`, `Terminating`
  или `Exited`, вид завершения 0 или 1 (только для `Exited`).
- Ответ с ошибкой должен содержать не менее 4 байтов; код ошибки 1…13. Лишние
  байты после кода игнорируются. Код передаётся в callback без изменений, в том
  числе `CORE_UNSUPPORTED`, если `appserv` отверг версию протокола.
- Нарушение любого из этих правил, ответ без запроса и неожиданный тип ответа
  считаются нарушением протокола: соединение закрывается, все запросы очереди
  получают callback с `CORE_IO_ERROR`, в `logserv` пишется запись.

Соединение и очередь:

- Соединение создаётся лениво при первом запросе и остаётся открытым до отказа
  или `app_destroy()`. Если `ipc_connection_begin_connect()` вернула ошибку,
  `app_apps_*()` возвращает её же (`CORE_NOT_FOUND`, если endpoint отсутствует),
  запрос не ставится, следующая операция пробует подключиться снова.
- Пока соединение в состоянии `Connecting`, запросы хранятся в очереди framework
  и отправляются после перехода в `Open`: в `ipclib` отправка допустима только
  в `Open`.
- Очередь ограничена `APP_APPS_MAX_PENDING`. Переполнение очереди framework и
  `CORE_OVERFLOW` от `ipclib` возвращаются как `CORE_WOULD_BLOCK`.
- Отказ соединения (закрытие пира, ошибка сокета, неудачное подключение,
  нарушение протокола) завершает все запросы очереди callback с `CORE_IO_ERROR`
  в порядке постановки. Следующая операция создаёт новое соединение.

## Протокол клиента logserv

Запись `log.submit` (`message_type` 1): `u16` версия, `i32` PID, `i32` TID,
`u8` уровень, `u16` длина component, `u32` длина message, component, message.
PID и TID берутся при вызове `app_log()` через `getpid()` и `gettid()`.
Ответ `log.submit_result` (`message_type` 2) — `ErrorCode`, ровно 4 байта.

- `app_log()` проверяет аргументы до постановки: `NULL`, уровень вне 0…5,
  длина более `APP_LOG_MESSAGE_MAX_LENGTH` или некорректный UTF-8 дают
  `CORE_INVALID_ARGUMENT`. Некорректный UTF-8 не отправляется никогда: запись
  с ним не принимается `loglib` и может остановить `logserv`.
- Возврат `CORE_OK` означает, что запись принята в очередь framework или
  `ipclib`, а не что `logserv` её сохранил.
- Соединение создаётся лениво при первой записи, включая запись о создании
  `AppHandle`; ошибка этой записи не прерывает `app_create()`. Если
  `ipc_connection_begin_connect()` вернула ошибку, её код возвращается из
  `app_log()`, запись отбрасывается, следующая запись пробует подключиться снова.
- Пока соединение в состоянии `Connecting`, записи хранятся в очереди
  framework и отправляются после перехода в `Open`. Число записей, ожидающих
  отправки или подтверждения, ограничено `APP_LOG_MAX_PENDING`; переполнение
  (в том числе `CORE_OVERFLOW` от `ipclib`) возвращает `CORE_WOULD_BLOCK`.
- Каждый ответ `logserv` уменьшает счётчик неподтверждённых записей. Ошибка в
  ответе (например, `CORE_WOULD_BLOCK` при полной очереди `logserv`) означает
  потерю записи: она не отправляется повторно и не журналируется.
- Лишний ответ, ответ неверного типа или размера, закрытие пира и ошибка
  сокета закрывают соединение; все записи, не подтверждённые `logserv`,
  отбрасываются без повторной отправки. Это не влияет на `app_run()`.

## Контракты

1. Все публичные операции возвращают `ErrorCode` из `sys/core`; `CORE_OK`
   — единственный успешный результат.
2. В процессе одновременно существует не более одного `AppHandle`: повторный
   `app_create()` до `app_destroy()` возвращает `CORE_INVALID_STATE`. После
   `app_destroy()` можно создать новый `AppHandle`. Каждый созданный handle
   уничтожается ровно один раз.
3. Поток-владелец — тот, что вызвал `app_create()`. `app_run()`, регистрация
   источников, все остальные операции и все callback выполняются только в нём.
   Исключение — `app_request_quit()`.
4. `app_request_quit()` допустим из обработчика `SIGTERM` и из любого потока:
   он устанавливает флаг выхода, пробуждает event loop и сохраняет `errno`.
   Другие публичные операции из обработчика сигнала не вызываются.
5. Callback не выполняет длительные блокирующие операции. Из callback и
   обработчиков событий допустимы `app_log()`, `app_apps_*()`,
   `app_event_add_fd()`, `app_event_create_timer()`, `app_event_remove()` и
   `app_request_quit()`; `app_run()` и `app_destroy()` возвращают
   `CORE_INVALID_STATE`.
6. `app_apps_*()` передаёт только `application_id` или `AppInstanceId`.
   Путь ELF, аргументы и environment остаются внутри `NativeAppSpec appserv`.
7. Каждый принятый запрос (`CORE_OK` из `app_apps_*()`) получает ровно один
   callback. Callback вызываются в порядке постановки запросов.
8. `app_log()` не завершает приложение при недоступности `logserv`; она
   возвращает ошибку, а следующая запись повторяет попытку подключения.
9. `appframework` не владеет FD, переданным в `app_event_add_fd()`.
10. Если установка обработчика через `sigaction()` завершается ошибкой,
    `app_create()` сохраняет `errno`, освобождает уже созданные ресурсы и
    преобразует ошибку через `core_error_from_errno()`. `app_destroy()`
    восстанавливает обработчик `SIGTERM`, который был до `app_create()`.
11. Обработчик `SIGTERM` только устанавливает флаг, вызывает
    `eventloop_wake()` и сохраняет `errno`. Запись о получении сигнала
    делается из event loop, не из обработчика.
12. Если выход запрошен до `app_run()`, `app_run()` возвращается сразу с
    `CORE_OK`.

## Конфигурация

Единственная конфигурация framework — `AppConfig`, передаваемая приложением
при `app_create()`. Библиотека не хранит файловый каталог приложений,
пользовательские данные или постоянную конфигурацию.

## Логирование и диагностика

Framework передаёт в `logserv` создание и уничтожение `AppHandle`,
недоступность `appserv`, отказ IPC-соединения, нарушение протокола,
ошибку event loop и обработку `SIGTERM`. Недоступность самого `logserv`
записать некуда: об этом сообщает только код возврата `app_log()`.
Содержимое UI-событий и прикладные данные он не журналирует.

Уровни внутренних записей: `Info` — создание, уничтожение и `SIGTERM`;
`Warning` — недоступность сервиса и отказ соединения; `Error` — нарушение
протокола и ошибка event loop. Компонент записи — `log_component` из
`AppConfig`.

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | `NULL`, пустой или слишком длинный id, неверный callback, идентификатор, уровень, маска или некорректный UTF-8 |
| `CORE_INVALID_STATE` | Операция вызвана в недопустимом состоянии или потоке; повторный `app_create()`; завершение запроса при `app_destroy()` |
| `CORE_NOT_FOUND` | Endpoint требуемого сервиса отсутствует; источник события не найден |
| `CORE_OUT_OF_MEMORY` | Нельзя создать контекст, очередь или копию ответа |
| `CORE_WOULD_BLOCK` | IPC-сообщение нельзя немедленно поставить в очередь, очередь запросов или записей переполнена |
| `CORE_IO_ERROR` | Ошибка event loop, соединения или повреждённый IPC-кадр; завершение запросов при отказе соединения |
| `CORE_UNSUPPORTED` | Сервис ответил, что версия протокола не равна v1 |

Остальные коды `ErrorCode` из ответа `appserv` (`CORE_NOT_FOUND`,
`CORE_INVALID_STATE` и другие) передаются в callback без изменений.

## Пример использования

```text
AppHandle app = NULL;
AppConfig config = { .application_id = "desktop", .log_component = "desktop" };

app_create(&config, &app);
ui_initialize(app);
app_apps_list(app, desktop_on_apps_list, NULL, NULL);
app_run(app);
ui_shutdown();
app_destroy(&app);
```

## Решения при уточнении спецификации

Эти пункты закрывают места, которые первоначальная версия оставляла неопределёнными
или в которых она противоречила сама себе. Они входят в контракт; список нужен
для проверки командой.

| # | Что было неясно | Принятое решение |
| --- | --- | --- |
| 1 | `app_destroy(app)` в таблице и `app_destroy(&app)` в примере | Параметр — `AppHandle *`; операция записывает `NULL`, как `*_close()` в `ipclib` |
| 2 | Нет сигнатур callback, типа уровня журнала, структур `AppInfo` и статуса | Добавлены типы `AppEventHandler`, `AppAppsCallback`, `AppAppsResult`, `AppInstanceStatus`, `AppLogLevel` и перечисления с фиксированными значениями |
| 3 | `CORE_UNSUPPORTED` по версии протокола: в ответах `appserv` и `logserv` нет поля версии | Версию проверяет сервис; клиент получает `CORE_UNSUPPORTED` только как код ошибки в ответе |
| 4 | «Ровно один `AppHandle`, по одному разу»: один раз за процесс или по разу на handle | Одновременно не более одного; после `app_destroy()` можно создать новый |
| 5 | Судьба обработчика `SIGTERM` после уничтожения | `app_destroy()` восстанавливает прежний обработчик |
| 6 | `SIGTERM` до `app_run()` | `app_run()` сразу возвращает `CORE_OK` |
| 7 | Висящие запросы при `app_destroy()` | Каждый получает ровно один callback с `CORE_INVALID_STATE` |
| 8 | Переполнение очереди запросов и `CORE_OVERFLOW` от `ipclib` | Возвращается `CORE_WOULD_BLOCK` |
| 9 | Переподключение к `appserv` после отказа | Висящие запросы получают `CORE_IO_ERROR`; следующая операция подключается заново |
| 10 | `out_request == NULL` (используется в примере) | Допустим |
| 11 | `AppInstanceId`: `appserv` определяет его у себя, а `libcore` — нет | `AppInstanceId` определён в `appframework/app.h` как `u64`; идентичный `typedef` допустим в C11 |
| 12 | Состояние `Failed` | Ошибка ожидания event loop, кроме `CORE_INTERRUPTED`; отказ соединения с сервисом в `Failed` не ведёт |
| 13 | Имя архива: `libappframework.a` в спецификации, `libpunos_*.a` у остальных библиотек | Сохранено имя из спецификации — `libappframework.a` |
| 14 | `app_request_quit()` допускалась из обработчика сигнала, но потоковая модель требовала поток-владельца | Допустим из любого потока и из обработчика сигнала |
| 15 | Некорректный UTF-8 в записи `app_log()` останавливает хранилище `loglib` | `app_log()` проверяет UTF-8 сама и никогда не отправляет такие записи |
| 16 | `app_destroy()` из callback | Запрещён (`CORE_INVALID_STATE`); освобождение контекста во время диспетчеризации небезопасно |
| 17 | Какие операции допустимы в каком состоянии | Таблица допустимости операций по состояниям |
| 18 | Поля `exit_kind` и `exit_value` ответа `get_status`, когда экземпляр не `Exited` | Клиент их не проверяет и не передаёт: для `Running` и `Terminating` в результате `Exited`/`0` |
| 19 | Пустое `display_name` в `AppInfo` | Допустимо (0..64 байт); пустой `application_id` — нарушение протокола |
| 20 | Нехватка памяти при разборе ответа `appserv` | Callback этого запроса получает `CORE_OUT_OF_MEMORY`; соединение остаётся, остальные запросы не затронуты |
| 21 | Нехватка памяти при отправке записи в `logserv` | `app_log()` возвращает ошибку, соединение остаётся; отказ сокета при `flush` закрывает соединение |
| 22 | Повторный `app_destroy()` из callback, вызванного самим `app_destroy()` | `CORE_INVALID_STATE`; без этого флага (`destroying`) вызов рекурсивен |

Детали реализации, не входящие в контракт: пачка ожидания event loop содержит
не более 16 событий; внутренние функции, нужные тестам, имеют префикс
`libappframework_internal_` и объявлены в `src/internal.h`.
Для тестов уровня 2 и 3 адреса `logserv` и `appserv` лежат в изменяемых переменных
`libappframework_internal_logserv_endpoint` и `libappframework_internal_appserv_endpoint`;
приложение их не использует. Имена самотестов в образе — `appfw-<имя>-selftest`
(дефис в сообщении `initserv` недопустим: его ищет `check-qemu-boot.sh`).
