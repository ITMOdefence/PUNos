## Описание

appserv — системный сервис управления нативными пользовательскими приложениями
PUNos. Он предоставляет доверенному Desktop или appframework каталог разрешённых
приложений, запускает их по application_id и владеет ProcessHandle каждого
запущенного экземпляра.

Для appserv любое приложение является нативным ELF-файлом. Сервис не знает,
на каком языке оно написано, не интерпретирует его данные и не зависит от его
внутреннего runtime.

## Имя сервиса

| Поле | Значение |
| --- | --- |
| Идентификатор | appserv |
| Исполняемый файл | appserv |
| Владелец запуска | initserv |
| IPC endpoint | /run/punos/appserv.sock |
| Версия протокола | v1 |

## Ответственность и границы

- appserv хранит каталог NativeAppSpec разрешённых нативных приложений.
- appserv принимает application_id, находит соответствующую запись каталога и
  запускает только указанный в ней ELF-файл через process_start.
- appserv создаёт AppInstanceId, хранит ProcessHandle экземпляра, отслеживает
  его завершение через process_poll и сообщает клиентам его состояние.
- appserv отправляет SIGTERM через process_terminate по запросу app.stop.
- appserv не принимает путь к исполняемому файлу, аргументы запуска или
  переменные окружения от IPC-клиента.
- appserv не управляет системными сервисами: их запускает и перезапускает
  initserv.
- appserv не знает языки программирования, пользовательские данные, разрешения
  устройств, внутренний runtime приложения или протоколы других сервисов.
- appserv не является посредником между запущенным приложением и IOserv,
  camserv, locationserv либо logserv.

## Зависимости

| Зависимость | Использование |
| --- | --- |
| sys/core | ErrorCode, AppInstanceId, базовые типы и проверка размеров IPC payload |
| sys/memory | Состояния экземпляров, IPC-ответы и временные таблицы каталога |
| sys/string | application_id, display_name, executable, arguments, environment и IPC endpoint |
| sys/process | Запуск, неблокирующий контроль и SIGTERM дочерних приложений |
| sys/event | Один event loop для endpoint, соединений и таймера process_poll |
| sys/time | Период мониторинга дочерних процессов |
| special/ipc | Endpoint, соединения, сообщения и исходящие очереди |
| logserv | Журналирование запуска, остановки и завершения экземпляра |

NativeAppSpec генерируется при сборке из NativeAppLaunchSpec статического
PlatformRegistry initserv. Сборка требует точного совпадения application_id и
executable в обоих представлениях и встраивает общий PlatformRegistryId.
Поэтому appserv получает уже разрешённый каталог, а `sys/process` остаётся
нейтральной низкоуровневой библиотекой и не пытается проверять реестр, которого
не видит. Регистрация разрешает запуск приложению через appserv, но не обязывает
initserv запускать его при загрузке ОС.

## Жизненный цикл

### Запуск

| Параметр | Значение |
| --- | --- |
| Готовые сервисы до запуска | logserv |
| Успех | Проверены PlatformRegistryId и NativeAppSpec; созданы endpoint, event loop и таймер мониторинга |
| Ошибка каталога или инициализации | Endpoint не создаётся; завершение с PROCESS_EXIT_INITIALIZATION_FAILED |

appserv не запускает приложения при собственном старте. Он переходит в Ready и
ожидает app.launch.

### Остановка и восстановление

При переходе в Stopping сервис прекращает приём новых IPC-клиентов и вызовов
app.launch. Для каждого активного экземпляра он вызывает process_terminate,
после чего вызывает process_wait для его ProcessHandle. Затем освобождает
состояния экземпляров, закрывает endpoint и event loop.

Appserv не перезапускает пользовательское приложение после его завершения:
новый экземпляр создаётся только новым app.launch. При аварийном завершении
appserv initserv может перезапустить сервис, но ранее запущенные дочерние
процессы не восстанавливаются и их состояния теряются.

Каждое приложение из NativeAppSpec обязано корректно завершать свой процесс
после SIGTERM. Это обязательное условие каталога v1: appserv вызывает
process_wait только после process_terminate и не использует SIGKILL либо
таймауты.

### Состояния сервиса

| Состояние | Допустимые действия | Переходы |
| --- | --- | --- |
| Starting | Создание endpoint, event loop и таймера | Ready, Failed |
| Ready | Каталог, запуск, остановка и статус экземпляров | Stopping, Failed |
| Stopping | SIGTERM, process_wait и освобождение ресурсов | Stopped |
| Failed | Новые операции не принимаются | Завершение процесса |

### Состояния экземпляра

| Состояние | Значение |
| --- | --- |
| Starting | process_start ещё выполняется; идентификатор экземпляра не выдан |
| Running | process_start успешен, ProcessHandle принадлежит appserv |
| Terminating | appserv отправил SIGTERM и ожидает завершение |
| Exited | process_poll или process_wait получил ProcessExitStatus |

Успешный app.launch означает только успешный запуск нативного процесса. Он не
означает предметную готовность приложения.

## Владение ресурсами

| Ресурс | Владелец | Создание | Освобождение |
| --- | --- | --- | --- |
| NativeAppSpec | appserv, статический каталог | До запуска | При завершении appserv |
| ProcessHandle экземпляра | appserv | После process_start | Только process_poll или process_wait |
| AppInstanceId и статус | appserv | После process_start | После app.stop и завершения либо остановки appserv |
| IpcServerHandle и IpcConnectionHandle | appserv через ipclib | При запуске и accept | При отключении или остановке |
| EventLoopHandle и таймер | appserv | При запуске | При остановке |

AppInstanceId уникален, пока экземпляр существует в памяти appserv. Он не
переиспользуется в рамках одного запуска сервиса и не является PID.

## IPC-контракт

### Клиенты и доступ

| Клиент | Разрешённые операции | Проверка доступа |
| --- | --- | --- |
| Desktop | app.list, app.get_info, app.launch, app.stop, app.get_status | Доверенный системный клиент v1 |
| appframework | app.list, app.get_info, app.launch, app.stop, app.get_status | Доверенный системный клиент v1 |

Запущенные приложения не используют appserv как прокси для доступа к другим
сервисам. В ipclib пока нет peer identity, поэтому endpoint appserv является
внутренним API доверенных компонентов, а не публичным endpoint приложения.

### Формат payload

Каждое сообщение — один кадр ipclib. Все целые поля payload little-endian;
строки имеют UTF-8 без NUL. IPC-клиент не передаёт путь к ELF-файлу, аргументы
процесса, переменные окружения или данные приложения.

| message_type | Направление | Payload |
| --- | --- | --- |
| 1, app.list | Клиент → сервис | Пустой |
| 2, app.list_result | Сервис → клиент | AppListResult |
| 3, app.get_info | Клиент → сервис | AppIdRequest |
| 4, app.get_info_result | Сервис → клиент | AppInfoResult |
| 5, app.launch | Клиент → сервис | AppIdRequest |
| 6, app.launch_result | Сервис → клиент | AppLaunchResult |
| 7, app.stop | Клиент → сервис | AppInstanceRequest |
| 8, app.stop_result | Сервис → клиент | AppStopResult |
| 9, app.get_status | Клиент → сервис | AppInstanceRequest |
| 10, app.get_status_result | Сервис → клиент | AppStatusResult |

### Операции

| Операция | Запрос | Успешный ответ | Назначение |
| --- | --- | --- | --- |
| app.list | Пустой payload | AppListResult | Возвращает весь статический каталог |
| app.get_info | AppIdRequest | AppInfoResult | Возвращает метаданные одного приложения |
| app.launch | AppIdRequest | AppLaunchResult | Запускает один новый экземпляр приложения |
| app.stop | AppInstanceRequest | AppStopResult | Отправляет SIGTERM экземпляру |
| app.get_status | AppInstanceRequest | AppStatusResult | Возвращает текущее или последнее известное состояние |

### Типы сообщений

| Тип | Поля | Ограничения | Назначение |
| --- | --- | --- | --- |
| AppIdRequest | u16 protocol_version, u16 application_id_length, u8 application_id[] | Версия 1; непустой UTF-8 id длиной не более 64 байт | Выбор приложения из каталога |
| AppInstanceRequest | u16 protocol_version, u64 instance_id | Версия 1; ненулевой id | Выбор запущенного экземпляра |
| AppInfo | u16 application_id_length, u8 application_id[], u16 display_name_length, u8 display_name[] | UTF-8; каждое поле не более 64 байт | Публичные метаданные каталога |
| AppListResult | ErrorCode error, u16 count, AppInfo[count] | error равен CORE_OK при успехе; не более 64 приложений | Весь каталог |
| AppInfoResult | ErrorCode error, AppInfo info | info определён только при CORE_OK | Данные одного приложения |
| AppLaunchResult | ErrorCode error, u64 instance_id | instance_id определён только при CORE_OK | Результат запуска |
| AppStopResult | ErrorCode error | CORE_OK означает отправку SIGTERM, не завершение процесса | Результат запроса остановки |
| AppStatusResult | ErrorCode error, u8 state, u8 exit_kind, i32 exit_value | Статус определён только при CORE_OK; exit поля определены только для Exited | Состояние экземпляра |

В NativeAppSpec остаются внутренними полями executable, default_arguments и
environment. Они не сериализуются и не передаются клиенту.

## Контракты

1. Все ошибки IPC-ответов являются ErrorCode из libcore; CORE_OK — единственный
   успешный код.
2. AppIdRequest выбирает только запись NativeAppSpec. Путь к ELF, аргументы и
   environment всегда берутся из этой записи и не подменяются клиентом.
3. До создания endpoint appserv проверяет PlatformRegistryId, уникальность
   application_id, непустой executable и структурную корректность каждого
   NativeAppSpec. Несовпадение с PlatformRegistry является ошибкой сборки;
   ошибочный встроенный каталог не запускает appserv.
4. ProcessHandle принадлежит appserv и применяется только к дочернему процессу
   данного appserv.
5. Каждый ProcessHandle завершённого экземпляра освобождается ровно одним
   process_poll или process_wait.
6. app.stop вызывает process_terminate и возвращает ответ после отправки
   SIGTERM. Каждый экземпляр обязан завершиться после SIGTERM; затем appserv
   освобождает его ProcessHandle через process_poll или process_wait.
7. Appserv не перезапускает приложение автоматически и не запускает его
   повторно после аварийного завершения.
8. Состояние Exited и ProcessExitStatus сохраняются до окончания текущего
   запуска appserv, поэтому app.get_status может вернуть результат завершения.
9. Все операции с ProcessHandle, каталогом и IPC handle выполняются одним
   потоком-владельцем event loop.
10. Appserv не получает и не интерпретирует данные, которые приложение
   использует после запуска.

## Потоковая модель и асинхронность

Appserv v1 однопоточный. Один поток владеет event loop, endpoint, всеми
IpcConnectionHandle, каталогом NativeAppSpec и ProcessHandle экземпляров.

Таймер event loop с периодом 1000 ms вызывает process_poll для каждого
экземпляра в состоянии Running или Terminating. Основной поток не вызывает
process_wait, кроме состояния Stopping.

## Конфигурация и постоянные данные

| Данные | Источник | Владелец | Действие при ошибке |
| --- | --- | --- | --- |
| Каталог NativeAppSpec и PlatformRegistryId | Сгенерированы при сборке из PlatformRegistry | appserv | Несовпадение или некорректная запись запрещают запуск appserv до создания endpoint |
| Экземпляры и их статусы | Память appserv | appserv | Теряются при перезапуске appserv |

Установка, удаление, обновление приложений, файловый manifest и пользовательские
аргументы запуска в v1 отсутствуют.

## Логирование и наблюдаемость

После готовности logserv appserv записывает запуск и завершение экземпляра,
application_id, AppInstanceId, ProcessExitStatus, ошибку запуска, запрос
остановки и недопустимый IPC-запрос. Сервис не записывает в журнал аргументы
приложения, его данные или содержимое IPC других сервисов.

## Ошибки

| ErrorCode | Когда возвращается клиенту |
| --- | --- |
| CORE_INVALID_ARGUMENT | Payload неверного размера, пустой application_id, нулевой instance_id или неизвестный message_type |
| CORE_NOT_FOUND | application_id отсутствует в каталоге либо instance_id отсутствует |
| CORE_INVALID_STATE | app.stop вызван для Exited экземпляра |
| CORE_UNSUPPORTED | protocol_version отличается от 1 |
| CORE_OUT_OF_MEMORY | Нельзя создать состояние экземпляра, ProcessHandle или IPC-ответ |
| CORE_IO_ERROR | Ошибка IPC или process_start не может запустить приложение |

## Ограничения версии

- Appserv управляет только нативными ELF-файлами из статически сгенерированного каталога.
- Нет установки, обновления, удаления, файловых manifest и пользовательских
  аргументов запуска.
- Нет специального знания о языках или runtime приложений.
- Нет достоверной идентичности IPC-клиента; endpoint только для доверенных
  Desktop и appframework.
- Нет автоматического перезапуска, SIGKILL и пользовательски настраиваемого
  timeout остановки: все приложения v1 обязаны завершаться по SIGTERM.
