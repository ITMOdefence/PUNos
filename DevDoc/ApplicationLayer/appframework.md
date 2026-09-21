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
| `sys/event` | Event loop, IPC-дескрипторы и таймеры |
| Syscall Wrappers | `sigaction()` для `SIGTERM` |
| `special/ipc` | Неблокирующие клиентские IPC-соединения |
| `logserv` | Записи `app_log()` через `/run/punos/logserv.sock` |
| `appserv` | Каталог и жизненный цикл приложений через `/run/punos/appserv.sock` |
| `uilib` | Не является зависимостью: `uilib` использует `appframework` |

## Интеграция с приложением

CRT передаёт управление функции `main()`. Нативное приложение создаёт один
`AppHandle`, настраивает собственный код и `uilib`, запускает `app_run()`,
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
`appserv`, включая `ErrorCode`, приходит в callback.

В `ipclib` v1 нет идентичности IPC-клиента. Поэтому особая роль графической
оболочки пока является соглашением доверенных компонентов: `appserv` не может
технически отличить её от другого процесса с доступом к своему endpoint.

## Типы

| Тип | Представление | Владение | Назначение |
| --- | --- | --- | --- |
| `AppHandle` | `struct AppHandle *` | Создаёт `app_create()`, уничтожает `app_destroy()` | Контекст одного приложения |
| `AppConfig` | `application_id`, `log_component` | Строки нужны только во время `app_create()` | Начальная конфигурация |
| `AppEventSourceId` | Ненулевой `u64` | До удаления источника или `app_destroy()` | Идентификатор источника event loop |
| `AppEventHandler` | Callback `(app, source, mask, context)` | Не владеет данными события | Обработчик FD или таймера |
| `AppAppsCallback` | Callback результата `appserv` | Ответ действителен только во время callback | Обработчик асинхронного ответа |
| `AppRequestId` | Ненулевой `u64` | До callback | Связывает запрос и ответ `appserv` |
| `AppInfo` | `application_id`, `display_name` | Принадлежит framework на время callback | Элемент каталога |
| `AppInstanceId` | Ненулевой `u64` | Не является PID | Идентификатор экземпляра от `appserv` |

Поля `application_id` и `display_name` — UTF-8 без NUL длиной не более 64
байтов: это ограничение IPC-контракта `appserv`.

## Публичные операции

| Операция | Возвращаемое значение | Назначение |
| --- | --- | --- |
| `app_create(config, out_app)` | `ErrorCode` | Создаёт контекст, event loop и обработчик `SIGTERM` |
| `app_run(app)` | `ErrorCode` | Выполняет event loop до запроса выхода или неустранимой ошибки |
| `app_request_quit(app)` | `ErrorCode` | Запрашивает штатный выход из `app_run()` |
| `app_destroy(app)` | `ErrorCode` | Закрывает IPC-клиенты и освобождает контекст |
| `app_event_add_fd(app, fd, mask, handler, context, out_source)` | `ErrorCode` | Регистрирует невладеющий FD |
| `app_event_create_timer(app, delay_ms, period_ms, handler, context, out_source)` | `ErrorCode` | Регистрирует таймер |
| `app_event_remove(app, source)` | `ErrorCode` | Удаляет источник; внешний FD не закрывается |
| `app_log(app, level, message)` | `ErrorCode` | Неблокирующе отправляет запись в `logserv` |
| `app_apps_list(app, callback, context, out_request)` | `ErrorCode` | Запрашивает каталог у `appserv` |
| `app_apps_get_info(app, id, callback, context, out_request)` | `ErrorCode` | Запрашивает метаданные приложения |
| `app_apps_launch(app, id, callback, context, out_request)` | `ErrorCode` | Просит `appserv` запустить приложение |
| `app_apps_stop(app, instance, callback, context, out_request)` | `ErrorCode` | Просит `appserv` отправить SIGTERM |
| `app_apps_get_status(app, instance, callback, context, out_request)` | `ErrorCode` | Запрашивает состояние экземпляра |

## Модель событий

Один поток-владелец `AppHandle` ожидает события через `sys/event` и
последовательно вызывает обработчики.

| Источник | Кто регистрирует | Действие framework |
| --- | --- | --- |
| IPC `uilib` с графическими сервисами | `uilib` | Вызывает обработчик `uilib` |
| IPC `appserv` | `appframework` | Разбирает ответ и вызывает `AppAppsCallback` |
| IPC `logserv` | `appframework` | Передаёт доступные записи очереди |
| Таймер | Приложение или `uilib` | Вызывает зарегистрированный обработчик |
| `SIGTERM` | `appframework` | Запрашивает выход и пробуждает event loop |

## Жизненный цикл и владение ресурсами

| Состояние | Допустимые действия | Переходы |
| --- | --- | --- |
| `Created` | Настройка `uilib` и обработчиков | `Running`, `Stopping` |
| `Running` | События и асинхронные запросы | `Stopping`, `Failed` |
| `Stopping` | Закрытие источников и IPC-клиентов | `Destroyed` |
| `Failed` | Новые запросы не принимаются | `Stopping` |
| `Destroyed` | Нет | Нет |

| Ресурс | Владелец | Освобождение |
| --- | --- | --- |
| `AppHandle` и event loop | `appframework` | `app_destroy()` |
| Клиенты `logserv` и `appserv` | `appframework` | `app_destroy()` или отказ соединения |
| Внешний FD | Вызывающий код или `uilib` | Владелец закрывает его после `app_event_remove()` |
| Окна и виджеты | `uilib` | По контракту `uilib`, до `app_destroy()` |

После `SIGTERM` framework завершает `app_run()`. Приложение обязано вызвать
`app_destroy()` и вернуться из `main()`, выполняя требование `appserv` v1.

## Контракты

1. Все публичные операции возвращают `ErrorCode` из `sys/core`; `CORE_OK`
   — единственный успешный результат.
2. В v1 в процессе существует ровно один `AppHandle`; он создаётся и
   уничтожается ровно по одному разу.
3. `app_run()`, регистрация источников и все callback выполняются только
   в потоке-владельце `AppHandle`.
4. `app_request_quit()` допустим из обработчика `SIGTERM`; другие публичные
   операции из обработчика сигнала не вызываются.
5. Callback не выполняет длительные блокирующие операции.
6. `app_apps_*()` передаёт только `application_id` или `AppInstanceId`.
   Путь ELF, аргументы и environment остаются внутри `NativeAppSpec appserv`.
7. `app_log()` не завершает приложение при недоступности `logserv`; она
   возвращает ошибку, а следующая запись повторяет попытку подключения.
8. `appframework` не владеет FD, переданным в `app_event_add_fd()`.

## Конфигурация

Единственная конфигурация framework — `AppConfig`, передаваемая приложением
при `app_create()`. Библиотека не хранит файловый каталог приложений,
пользовательские данные или постоянную конфигурацию.

## Логирование и диагностика

Framework передаёт в `logserv` создание и уничтожение `AppHandle`,
недоступность сервисов, отказ IPC-соединения, ошибку event loop и обработку
`SIGTERM`. Содержимое UI-событий и прикладные данные он не журналирует.

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | `NULL`, пустой id, неверный callback или идентификатор |
| `CORE_INVALID_STATE` | Операция вызвана в недопустимом состоянии или потоке |
| `CORE_NOT_FOUND` | Endpoint требуемого сервиса отсутствует |
| `CORE_OUT_OF_MEMORY` | Нельзя создать контекст, очередь или копию ответа |
| `CORE_WOULD_BLOCK` | IPC-сообщение нельзя немедленно поставить в очередь |
| `CORE_IO_ERROR` | Ошибка event loop, соединения или повреждённый IPC-кадр |
| `CORE_UNSUPPORTED` | Версия протокола сервиса не равна v1 |

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
