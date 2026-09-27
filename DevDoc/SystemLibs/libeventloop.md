## Описание

Системная библиотека для ожидания событий от файловых дескрипторов, таймеров и
других потоков в одном цикле обработки.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libeventloop`. Готовы:

- публичный заголовок `<sys/eventloop.h>` (именно так, по `_LibsList.md`; в
  разделе «Имя библиотеки» этой страницы указано `sys/event` — используется
  первое, как того требует `System/SystemLibs/README.md`) со всеми типами,
  константами и операциями из таблиц ниже;
- статическая библиотека `libpunos_eventloop.a`: таблица источников
  (fd/timerfd/внутренний eventfd) растёт через `libmem`, `EventSourceId` —
  это `epoll_data.u64`, буфер под `epoll_wait()` переиспользуется между
  вызовами вместо `mmap`/`munmap` на каждый `eventloop_wait()`;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест (`tests/eventloop_runtime.c`): регистрация/снятие
  внешнего fd (через `eventfd()` напрямую — `pipe()` в Syscall Wrappers
  нет), однократный и периодический таймер (включая `delay_ms = 0`),
  `eventloop_wake()` из настоящего второго потока (через `libthread`,
  только для теста). Выполняется на хосте командой
  `make -C System/SystemLibs/libeventloop check` и внутри PUNos в QEMU как
  `/punos/bin/libeventloop-selftest`.

Библиотека вызывает только `epoll_create1()`, `epoll_ctl()`, `epoll_wait()`,
`eventfd()`, `timerfd_create()`, `timerfd_settime()`, `read()`, `write()` и
`close()` (контракт 1). Решения, принятые при реализации, описаны в
`System/SystemLibs/libeventloop/README.md`.

## Имя библиотеки
`sys/event`
## Контракты

1. Библиотека использует только Syscall Wrappers `epoll_create1()`, `epoll_ctl()`,
   `epoll_wait()`, `eventfd()`, `timerfd_create()`, `timerfd_settime()`, `read()`,
   `write()` и `close()`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. Созданный `EventLoopHandle` должен быть закрыт ровно один раз через
   `eventloop_close()`.
4. Event loop владеет созданными им `epoll`, `eventfd` и `timerfd`.
5. Event loop не владеет файловыми дескрипторами, переданными в
   `eventloop_add_fd()`; их закрывает владелец источника.
6. При `eventloop_remove_source()` для источника `Timer` библиотека закрывает
   созданный ей `timerfd`; для источника `FileDescriptor` файловый дескриптор
   не закрывается.
7. `EventSourceId` уникален, пока источник зарегистрирован в event loop.
8. `eventloop_add_fd()`, `eventloop_update_fd()`, `eventloop_remove_source()` и
   `eventloop_wait()` вызываются только из потока-владельца event loop.
9. `eventloop_wake()` допускается вызывать из другого потока.
10. Библиотека возвращает только факт готовности источника и не интерпретирует
   данные устройства, socket или таймера.
11. `DurationMs` используется для всех timeout. Значение
    `EVENTLOOP_WAIT_FOREVER` преобразуется в `-1` для `epoll_wait()`; конечный
    timeout больше `2147483647` миллисекунд возвращает `CORE_INVALID_ARGUMENT`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `EventLoopHandle` | Непрозрачный указатель: `struct EventLoopHandle *` | `NULL` или только открытый event loop | Идентификатор event loop |
| `EventSourceId` | `u64` | Ненулевой уникальный идентификатор | Идентификатор зарегистрированного источника |
| `DurationMs` | Тип из `libtime` | Неотрицательная длительность или `EVENTLOOP_WAIT_FOREVER` | Timeout и периоды таймеров |
| `EventMask` | Битовая маска `u32` | Комбинация флагов `EVENT_*` | События, которые интересуют вызывающий код или произошли |
| `EventSourceKind` | Перечисление | `FileDescriptor`, `Timer`, `Wake` | Вид источника события |
| `Event` | Структура: `EventSourceId source_id`, `EventSourceKind kind`, `EventMask mask` | Одно зарегистрированное событие | Результат ожидания event loop |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `EVENT_READABLE` | Источник готов к чтению |
| `EVENT_WRITABLE` | Источник готов к записи |
| `EVENT_ERROR` | Для источника произошла ошибка |
| `EVENT_HANGUP` | Соединение или источник закрыт |
| `EVENTLOOP_WAIT_FOREVER` | `18446744073709551615` (`u64` maximum): ожидать события без ограничения времени |

## Операции

| Операция                                                                   | Аргументы                                                                                                   | Возвращаемое значение | Назначение                                                                                                               |
| -------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- | --------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `eventloop_create(out_loop)`                                               | `EventLoopHandle *out_loop`                                                                                 | `ErrorCode`           | Создаёт event loop через `epoll_create1()` и внутренний источник пробуждения через `eventfd()`                           |
| `eventloop_close(loop)`                                                    | `EventLoopHandle *loop`                                                                                     | `ErrorCode`           | Закрывает event loop, внутренние дескрипторы и записывает `NULL`                                                         |
| `eventloop_add_fd(loop, fd, mask, out_source)`                             | `EventLoopHandle loop`, `i32 fd`, `EventMask mask`, `EventSourceId *out_source`                             | `ErrorCode`           | Регистрирует внешний файловый дескриптор через `epoll_ctl()`                                                             |
| `eventloop_update_fd(loop, source, mask)`                                  | `EventLoopHandle loop`, `EventSourceId source`, `EventMask mask`                                            | `ErrorCode`           | Изменяет набор ожидаемых событий зарегистрированного источника                                                           |
| `eventloop_remove_source(loop, source)`                                    | `EventLoopHandle loop`, `EventSourceId source`                                                              | `ErrorCode`           | Удаляет источник; внутренний `timerfd` закрывается, внешний файловый дескриптор — нет                                    |
| `eventloop_create_timer(loop, delay_ms, period_ms, out_source)`            | `EventLoopHandle loop`, `DurationMs delay_ms`, `DurationMs period_ms`, `EventSourceId *out_source`           | `ErrorCode`           | Создаёт и регистрирует таймер через `timerfd_create()` и `timerfd_settime()`; `period_ms = 0` создаёт одноразовый таймер |
| `eventloop_wait(loop, out_events, events_capacity, timeout_ms, out_count)` | `EventLoopHandle loop`, `Event *out_events`, `usize events_capacity`, `DurationMs timeout_ms`, `usize *out_count` | `ErrorCode`       | Ожидает события через `epoll_wait()`; `out_count = 0` означает timeout                                                   |
| `eventloop_wake(loop)`                                                     | `EventLoopHandle loop`                                                                                      | `ErrorCode`           | Пробуждает ожидающий event loop через внутренний `eventfd`                                                               |
