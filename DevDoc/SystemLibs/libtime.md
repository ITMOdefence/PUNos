## Описание

Системная библиотека для получения времени, работы с длительностями, deadline и
приостановки текущего потока.
## Имя библиотеки
`sys/time`
## Контракты

1. Библиотека использует только Syscall Wrappers `clock_gettime()` и
   `nanosleep()`, а также типы и операции проверки переполнения из `libcore`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. Все значения времени и длительности измеряются в миллисекундах.
4. `MonotonicTime` применяется для timeout, deadline и измерения длительности.
5. `WallClockTime` применяется только для меток событий; его нельзя
   использовать для deadline, так как системное время может быть изменено.
6. `time_sleep()` при прерывании сигналом возвращает `CORE_INTERRUPTED` и
   записывает неистёкшую длительность в `out_remaining`.
7. Создание таймеров, которые доставляют события в event loop, не входит в
   `libtime` и реализуется в `libeventloop`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `DurationMs` | `u64` из `libcore` | Неотрицательная длительность в миллисекундах | Длительность сна, timeout и периодов |
| `MonotonicTime` | `u64` из `libcore` | Момент монотонных часов в миллисекундах | Измерение интервалов и deadline |
| `WallClockTime` | `u64` из `libcore` | Миллисекунды с Unix epoch | Метка календарного времени события |
| `Deadline` | `MonotonicTime` | Момент монотонных часов | Абсолютный момент истечения timeout |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `TIME_MILLISECONDS_PER_SECOND` | Число миллисекунд в секунде: `1000` |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `time_monotonic_now(out_time)` | `MonotonicTime *out_time` | `ErrorCode` | Получает текущее монотонное время через `clock_gettime()` |
| `time_wall_clock_now(out_time)` | `WallClockTime *out_time` | `ErrorCode` | Получает календарное время через `clock_gettime()` |
| `time_sleep(duration, out_remaining)` | `DurationMs duration`, `DurationMs *out_remaining` | `ErrorCode` | Приостанавливает текущий поток через `nanosleep()`; при успехе записывает `0` в `out_remaining` |
| `time_deadline_after(duration, out_deadline)` | `DurationMs duration`, `Deadline *out_deadline` | `ErrorCode` | Создаёт deadline как сумму текущего монотонного времени и длительности |
| `time_deadline_expired(deadline, out_expired)` | `Deadline deadline`, `bool *out_expired` | `ErrorCode` | Проверяет по монотонным часам, истёк ли deadline |
| `time_deadline_remaining(deadline, out_remaining)` | `Deadline deadline`, `DurationMs *out_remaining` | `ErrorCode` | Возвращает оставшуюся длительность; после истечения записывает `0` |
