## Описание

Специальная библиотека для локального межпроцессного взаимодействия между
компонентами unaOS через неблокирующие Unix domain sockets. Библиотека создаёт
серверы и соединения, передаёт типизированные сообщения с границами кадров и
предоставляет файловые дескрипторы для регистрации в `sys/eventloop`.

## Имя библиотеки

`special/ipc`

## Реализация

Реализована в `System/SpecialLibs/ipclib`, публичный заголовок —
`<special/ipc.h>`, архив — `libpunos_ipc.a`. Проверка:
`make -C System/SpecialLibs/ipclib check`. Тесты проверяют неблокирующий обмен,
фрагментацию, владение payload, ограничения очереди и повреждённые кадры.

## Контракты

1. Библиотека предназначена только для локального IPC через `AF_UNIX` и
   `SOCK_STREAM`. Сетевые сокеты `AF_INET` и `AF_INET6` не поддерживаются.
2. Библиотека использует типы и `ErrorCode` из `libcore`, память из `libmem`,
   строки из `libstr`, а ожидание готовности дескрипторов выполняется
   вызывающим кодом через `libeventloop`.
3. До появления отдельной SystemLib для socket-операций библиотека использует
   только следующие Syscall Wrappers: `socket()`, `bind()`, `listen()`,
   `accept4()`, `connect()`, `getsockopt()`, `send()`, `recv()`, `shutdown()`,
   `close()` и `unlink()`. Другие Syscall Wrappers библиотеке недоступны.
4. Все операции возвращают `ErrorCode` из `libcore`. `CORE_OK` — единственный
   успешный результат.
5. Значения параметров с префиксом `out_` определены только при возврате
   `CORE_OK`, если в описании конкретной операции не сказано иное.
6. `IpcServerHandle` и `IpcConnectionHandle` являются непрозрачными объектами.
   Каждый созданный объект должен быть закрыт ровно один раз соответствующей
   операцией `ipc_server_close()` или `ipc_connection_close()`.
7. Библиотека владеет файловыми дескрипторами сервера и соединения. Дескриптор,
   возвращённый `ipc_server_get_fd()` или `ipc_connection_get_fd()`, является
   невладеющим; вызывающий код не должен закрывать его.
8. Перед созданием сервера `ipc_server_create()` безусловно удаляет объект,
   уже существующий по пути endpoint, через `unlink()`. Это позволяет
   сервису занять прежний endpoint после аварийного завершения предыдущего
   экземпляра. Для учебной ОС v1 библиотека не проверяет, принадлежал ли
   удалённый endpoint живому серверу. `ipc_server_close()` удаляет endpoint
   успешно созданного сервера.
9. Все сокеты создаются с `SOCK_NONBLOCK` и `SOCK_CLOEXEC`. Ни одна операция
   библиотеки не ожидает данные, подключение или освобождение места в буфере
   ядра.
10. Один server или connection принадлежит потоку, который создал объект.
    Одновременные вызовы с одним handle не допускаются. Синхронизацию между
    потоками обеспечивает вызывающий код через `libsync`.
11. Вызывающий код регистрирует невладеющий файловый дескриптор в `sys/eventloop`.
    `EVENT_READABLE` означает, что допустимо вызвать `ipc_server_accept()` или
    `ipc_connection_receive()`; `EVENT_WRITABLE` —
    `ipc_connection_finish_connect()` или `ipc_connection_flush()`.
12. Сообщение состоит из одного кадра. Библиотека не интерпретирует
    `message_type` и payload: семантику команд, версионирование конкретного
    сервиса, авторизацию и permissions определяют System Services и
    `appframework`.
13. Payload, переданный в `ipc_connection_send()`, копируется во внутреннюю
    очередь библиотеки до возврата из функции. Вызывающий код сохраняет
    владение исходным `Bytes`.
14. Payload, полученный через `ipc_connection_receive()`, принадлежит
    вызывающему коду и должен быть освобождён ровно один раз через
    `ipc_message_destroy()`.
15. `ipc_connection_close()` отбрасывает непереданные исходящие сообщения и
    неполный входящий кадр. Для штатного завершения вызывающий код обязан
    вызвать `ipc_connection_flush()` до закрытия.

### Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `libcore` | Типы, `Bytes`, `bool`, `ErrorCode` и проверка переполнения размеров |
| `libmem` | Выделение и освобождение handle, внутренних буферов и payload принятого сообщения |
| `libstr` | Проверка и хранение UTF-8 endpoint |
| `libeventloop` | Не является зависимостью линковки; вызывающий код использует его с дескрипторами, возвращёнными библиотекой |
| Syscall Wrappers из пункта 3 | Создание и обслуживание Unix socket |

### Формат кадра

Все целые поля записываются в little-endian порядке. Размер заголовка всегда
равен `IPC_FRAME_HEADER_SIZE`.

| Смещение | Поле | Представление | Назначение |
| --- | --- | --- | --- |
| `0` | `payload_size` | `u32` | Размер payload в байтах, без заголовка |
| `4` | `message_type` | `u32` | Тип сообщения, определяемый протоколом сервиса |
| `8` | `payload` | `u8[payload_size]` | Непрозрачные данные сообщения |

### Состояния соединения

| Состояние | Допустимые операции | Назначение |
| --- | --- | --- |
| `Connecting` | `ipc_connection_get_fd()`, `ipc_connection_finish_connect()`, `ipc_connection_close()` | Неблокирующее подключение начато, но ещё не подтверждено ядром |
| `Open` | `ipc_connection_get_fd()`, `ipc_connection_send()`, `ipc_connection_flush()`, `ipc_connection_receive()`, `ipc_connection_close()` | Двунаправленный обмен сообщениями доступен |
| `PeerClosed` | `ipc_connection_close()` | Удалённая сторона закрыла соединение; новые сообщения не принимаются |

Операция, не допустимая в текущем состоянии, возвращает
`CORE_INVALID_STATE`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| --- | --- | --- | --- |
| `IpcEndpoint` | `String` из `libstr` | UTF-8 путь без `NUL`, длиной не более `IPC_ENDPOINT_MAX_LENGTH` | Имя Unix socket endpoint |
| `IpcServerHandle` | Непрозрачный указатель: `struct IpcServerHandle *` | `NULL` или открытый сервер | Сервер, ожидающий локальные подключения |
| `IpcConnectionHandle` | Непрозрачный указатель: `struct IpcConnectionHandle *` | `NULL` или открытое/подключающееся соединение | Двунаправленное IPC-соединение |
| `IpcMessageType` | `u32` из `libcore` | Определяется протоколом конкретного сервиса | Идентификатор смысла сообщения |
| `IpcMessage` | Структура: `IpcMessageType type`, `Bytes payload` | Корректный кадр с payload не больше `IPC_MESSAGE_MAX_SIZE` | Полученное или отправляемое сообщение |
| `IpcConnectionState` | Перечисление | `Connecting`, `Open`, `PeerClosed` | Текущее состояние соединения |
| `IpcReceiveStatus` | Перечисление | `NoMessage`, `Message`, `PeerClosed` | Результат неблокирующей попытки приёма |

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `IPC_ENDPOINT_MAX_LENGTH` | `107` | Максимальная длина endpoint в байтах без завершающего `NUL` |
| `IPC_FRAME_HEADER_SIZE` | `8` | Размер заголовка IPC-кадра в байтах |
| `IPC_MESSAGE_MAX_SIZE` | `1048576` | Максимальный размер payload одного сообщения в байтах |
| `IPC_OUTGOING_QUEUE_MAX_SIZE` | `4194304` | Максимальный суммарный размер очереди исходящих кадров одного соединения в байтах |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| --- | --- | --- | --- |
| `ipc_server_create(endpoint, out_server)` | `const IpcEndpoint *endpoint`, `IpcServerHandle *out_server` | `ErrorCode` | Безусловно удаляет существующий объект по endpoint, создаёт неблокирующий Unix socket server, привязывает его к endpoint и переводит в состояние ожидания подключений |
| `ipc_server_get_fd(server, out_fd)` | `IpcServerHandle server`, `i32 *out_fd` | `ErrorCode` | Возвращает невладеющий файловый дескриптор сервера для регистрации в event loop |
| `ipc_server_accept(server, out_connection)` | `IpcServerHandle server`, `IpcConnectionHandle *out_connection` | `ErrorCode` | Принимает одно ожидающее подключение через `accept4()` с `SOCK_NONBLOCK` и `SOCK_CLOEXEC`; возвращает `CORE_WOULD_BLOCK`, если очередь подключений пуста |
| `ipc_server_close(server)` | `IpcServerHandle *server` | `ErrorCode` | Закрывает сервер, удаляет созданный им endpoint и записывает `NULL` |
| `ipc_connection_begin_connect(endpoint, out_connection)` | `const IpcEndpoint *endpoint`, `IpcConnectionHandle *out_connection` | `ErrorCode` | Создаёт неблокирующее клиентское соединение в состоянии `Connecting` |
| `ipc_connection_finish_connect(connection, out_state)` | `IpcConnectionHandle connection`, `IpcConnectionState *out_state` | `ErrorCode` | Проверяет `SOL_SOCKET`/`SO_ERROR` через `getsockopt()`; записывает `Open`, если соединение установлено, либо `Connecting`, если ядро ещё не завершило подключение |
| `ipc_connection_get_fd(connection, out_fd)` | `IpcConnectionHandle connection`, `i32 *out_fd` | `ErrorCode` | Возвращает невладеющий файловый дескриптор соединения для регистрации в event loop |
| `ipc_connection_send(connection, message)` | `IpcConnectionHandle connection`, `const IpcMessage *message` | `ErrorCode` | Проверяет размер, копирует кадр во внутреннюю исходящую очередь и не передаёт его в сокет непосредственно |
| `ipc_connection_flush(connection, out_has_pending)` | `IpcConnectionHandle connection`, `bool *out_has_pending` | `ErrorCode` | Передаёт максимальное доступное число исходящих байтов без блокировки; записывает `true`, если часть очереди ещё ожидает передачи |
| `ipc_connection_receive(connection, out_status, out_message)` | `IpcConnectionHandle connection`, `IpcReceiveStatus *out_status`, `IpcMessage *out_message` | `ErrorCode` | Читает доступные байты, собирает не более одного полного кадра и передаёт его вызывающему коду; при `NoMessage` и `PeerClosed` значение `out_message` не определено |
| `ipc_message_destroy(message)` | `IpcMessage *message` | `ErrorCode` | Освобождает payload сообщения через `libmem` и делает структуру недействительной |
| `ipc_connection_close(connection)` | `IpcConnectionHandle *connection` | `ErrorCode` | Закрывает соединение, освобождает внутренние буферы и записывает `NULL` |

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Передан `NULL`, пустой endpoint, недопустимый размер сообщения или значение вне контракта операции |
| `CORE_INVALID_PATH` | Endpoint не может быть представлен в `sockaddr_un` |
| `CORE_INVALID_STATE` | Операция не допускается в текущем состоянии server или connection |
| `CORE_ALREADY_EXISTS` | Endpoint появился повторно между его удалением и `bind()` |
| `CORE_NOT_FOUND` | Endpoint удалён до подключения или больше не существует |
| `CORE_OUT_OF_MEMORY` | Невозможно выделить handle, входящий буфер или очередь исходящих кадров |
| `CORE_WOULD_BLOCK` | Неблокирующая операция не может немедленно принять соединение |
| `CORE_IO_ERROR` | Ошибка сокета, некорректный кадр от peer или системная ошибка, не имеющая более точного `ErrorCode` |
| `CORE_OVERFLOW` | Размер кадра или очереди превышает установленный максимум |
