## Описание

Системная библиотека для синхронизации потоков через mutex, события и condition
variable.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libsync`. Готовы:

- публичный заголовок `<sys/sync.h>` со всеми типами и константами;
- статическая библиотека `libpunos_sync.a` со всеми операциями из таблицы
  ниже: трёхсостояньевый futex-mutex (Дреппер, «Futexes Are Tricky»),
  ручное событие и condition variable на счётчике поколений;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест (`tests/sync_runtime.c`) с реальными потоками (через
  `libthread`, только для теста — сама библиотека потоков не создаёт):
  контролируемая гонка на mutex (4 потока по 20000 инкрементов), таймауты
  `mutex_lock`/`condition_wait`, несколько потоков, разбуженных одним
  `event_signal()`/`condition_signal_all()`. Выполняется на хосте командой
  `make -C System/SystemLibs/libsync check` (30 раз подряд — гонки
  проявляются не каждый раз) и внутри PUNos в QEMU как
  `/punos/bin/libsync-selftest`.

Библиотека вызывает только `futex()` (контракт 1); память для handle'ов —
через `libmem`. Решения, принятые при реализации, описаны в
`System/SystemLibs/libsync/README.md`.

## Имя библиотеки
`sys/sync`
## Контракты

1. Библиотека использует только Syscall Wrapper `futex()` и типы с `ErrorCode`
   из `libcore`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. `Mutex`, `SyncEvent` и `Condition` создаются и уничтожаются ровно один раз.
4. Mutex является нерекурсивным: поток не должен повторно захватывать уже
   захваченный им mutex.
5. Mutex должен быть разблокирован тем же потоком, который его захватил.
6. `mutex_destroy()` допустим только для разблокированного mutex без ожидающих
   потоков.
7. `SyncEvent` является ручным событием: `event_signal()` оставляет его
   сигнальным до вызова `event_reset()`.
8. `condition_wait()` вызывается только при уже захваченном mutex; операция
   атомарно освобождает mutex, ожидает сигнал и захватывает mutex снова.
9. Значение timeout измеряется в миллисекундах. При его истечении операция
   возвращает `CORE_TIMEOUT`.
10. Все timeout имеют тип `DurationMs` из `libtime`; значение
    `SYNC_WAIT_FOREVER` означает отсутствие timeout.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `Mutex` | Непрозрачный указатель: `struct Mutex *` | `NULL` или созданный и не уничтоженный mutex | Взаимное исключение доступа к данным |
| `SyncEvent` | Непрозрачный указатель: `struct SyncEvent *` | `NULL` или сигнальное/несигнальное событие | Ожидание наступления события |
| `Condition` | Непрозрачный указатель: `struct Condition *` | `NULL` или созданный и не уничтоженный condition variable | Ожидание изменения состояния под mutex |
| `DurationMs` | Тип из `libtime` | Неотрицательная длительность или `SYNC_WAIT_FOREVER` | Timeout синхронизационных операций |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `SYNC_WAIT_FOREVER` | `18446744073709551615` (`u64` maximum): ожидать без timeout |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `mutex_create(out_mutex)` | `Mutex *out_mutex` | `ErrorCode` | Создаёт разблокированный mutex |
| `mutex_destroy(mutex)` | `Mutex *mutex` | `ErrorCode` | Уничтожает mutex |
| `mutex_lock(mutex, timeout_ms)` | `Mutex mutex`, `DurationMs timeout_ms` | `ErrorCode` | Захватывает mutex или возвращает `CORE_TIMEOUT` |
| `mutex_unlock(mutex)` | `Mutex mutex` | `ErrorCode` | Разблокирует ранее захваченный mutex |
| `event_create(out_event)` | `SyncEvent *out_event` | `ErrorCode` | Создаёт несигнальное ручное событие |
| `event_destroy(event)` | `SyncEvent *event` | `ErrorCode` | Уничтожает событие |
| `event_wait(event, timeout_ms)` | `SyncEvent event`, `DurationMs timeout_ms` | `ErrorCode` | Ожидает сигнальное состояние события или возвращает `CORE_TIMEOUT` |
| `event_signal(event)` | `SyncEvent event` | `ErrorCode` | Переводит событие в сигнальное состояние и пробуждает ожидающие потоки |
| `event_reset(event)` | `SyncEvent event` | `ErrorCode` | Переводит событие в несигнальное состояние |
| `condition_create(out_condition)` | `Condition *out_condition` | `ErrorCode` | Создаёт condition variable |
| `condition_destroy(condition)` | `Condition *condition` | `ErrorCode` | Уничтожает condition variable |
| `condition_wait(condition, mutex, timeout_ms)` | `Condition condition`, `Mutex mutex`, `DurationMs timeout_ms` | `ErrorCode` | Освобождает mutex, ожидает сигнал и повторно захватывает mutex |
| `condition_signal_one(condition)` | `Condition condition` | `ErrorCode` | Пробуждает один поток, ожидающий condition variable |
| `condition_signal_all(condition)` | `Condition condition` | `ErrorCode` | Пробуждает все потоки, ожидающие condition variable |
