## Описание

Системная библиотека для доступа к отдельному устройству Linux input subsystem
через `/dev/input/event*`. Инкапсулирует Linux evdev ABI, чтение сырых событий
и запрос базовых возможностей устройства, не интерпретируя их для UI.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libinputdevice`. Готовы:

- публичный заголовок `<sys/input-device.h>` со всеми типами и константами;
- статическая библиотека `libpunos_inputdevice.a`: Linux evdev ABI
  (`struct input_event`, `struct input_id`, `EVIOCGID`, `EVIOCGBIT`)
  написан вручную (в PUNos нет `<linux/input.h>`) и сверен построчно с
  `/usr/include/linux/input.h` и `input-event-codes.h`;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест (`tests/input_device_runtime.c`), который адаптируется к
  машине: всегда проверяет `CORE_NOT_FOUND` для заведомо несуществующего
  пути и все проверки `NULL`-аргументов; без прав — `CORE_PERMISSION_DENIED`
  на реальном `/dev/input/event*`; под root (внутри PUNos или через `sudo`)
  — полный успешный путь на реальном устройстве. Выполняется на
  хосте командой `make -C System/SystemLibs/libinputdevice check` и внутри
  PUNos в QEMU как `/punos/bin/libinputdevice-selftest`.

Библиотека вызывает только `open()`, `close()`, `read()` и `ioctl()`
(контракт 1). Решения, принятые при реализации (включая перепроверку
кодировки `ioctl` через `sudo` на реальном оборудовании), описаны в
`System/SystemLibs/libinputdevice/README.md`.

## Имя библиотеки

`sys/input-device`

## Контракты

1. Библиотека использует только Syscall Wrappers `open()`, `close()`, `read()`
   и `ioctl()`, а также типы и `ErrorCode` из `libcore`, память из `libmem`,
   строки из `libstr` и монотонное время из `libtime`.
2. Библиотека работает только с явно переданным путём `/dev/input/event*`.
   Поиск устройств, hot-plug, выбор устройства и permissions не входят в её
   ответственность.
3. Все операции возвращают `ErrorCode` из `libcore`.
4. Значения результата передаются через параметры с префиксом `out_*`.
   При возврате ошибки значения `out_*` не определены, кроме `out_status` в
   `input_raw_device_read()`: он определён при `CORE_OK`, а `out_events` и
   `out_count` определены только при статусе `EventsReady`.
5. `InputRawDeviceHandle` является непрозрачным указателем. Созданный handle
   должен быть закрыт ровно один раз через `input_raw_device_close()`.
6. Библиотека владеет файловым дескриптором устройства. Дескриптор,
   возвращённый `input_raw_device_get_fd()`, является невладеющим; вызывающий
   код не закрывает его.
7. Устройство открывается с `O_NONBLOCK` и `O_CLOEXEC`.
   `input_raw_device_read()` не ожидает события: при отсутствии данных она
   возвращает статус `NoEvents`.
8. `input_raw_device_read()` возвращает события в том же порядке, в котором
   Linux input subsystem передала их драйверу. Библиотека не группирует события
   по `SYN_REPORT` и не отбрасывает неизвестные `type`, `code` или `value`.
9. `InputRawEvent.timestamp` получается через `time_monotonic_now()` при
   чтении события. Это момент получения события библиотекой, а не исходная
   временная метка ядра Linux.
10. Библиотека не преобразует коды клавиш в символы, не интерпретирует жесты,
    не преобразует координаты и не маршрутизирует события.
11. Указатели на структуры Linux evdev передаются третьим аргументом
    `ioctl()` только после явного преобразования через `uintptr_t` в
    `unsigned long`. Библиотека не предполагает, что третий аргумент wrapper
    имеет тип указателя.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `String` | Тип из `libstr` | UTF-8 строка без `NUL` | Путь к устройству input subsystem |
| `MonotonicTime` | Тип из `libtime` | Момент монотонных часов в миллисекундах | Момент получения raw-события |
| `InputRawDeviceHandle` | Непрозрачный указатель: `struct InputRawDeviceHandle *` | `NULL` или открытое устройство | Контекст одного `/dev/input/event*` |
| `InputRawDeviceConfig` | Структура: `const String *path` | Непустой путь `/dev/input/event*` без `NUL` | Конфигурация открытия устройства |
| `InputRawCapabilityMask` | Битовая маска `u32` | Комбинация `INPUT_RAW_CAP_*` | Группы raw-событий, поддерживаемые устройством |
| `InputRawDeviceInfo` | Структура: `u16 bus`, `u16 vendor`, `u16 product`, `u16 version`, `InputRawCapabilityMask capabilities` | Идентификаторы Linux input device и его возможности | Базовые метаданные устройства |
| `InputRawEvent` | Структура: `u16 type`, `u16 code`, `i32 value`, `MonotonicTime timestamp` | Значения Linux evdev без преобразования | Одно сырое событие устройства |
| `InputRawReadStatus` | Перечисление | `EventsReady`, `NoEvents`, `DeviceLost` | Результат неблокирующей попытки чтения raw-событий |

## Константы

| Наименование | Значение | Назначение |
| ------------ | -------- | ---------- |
| `INPUT_RAW_EVENT_SYN` | `0` | Значение `InputRawEvent.type` для синхронизации |
| `INPUT_RAW_EVENT_KEY` | `1` | Значение `InputRawEvent.type` для клавиш и кнопок |
| `INPUT_RAW_EVENT_REL` | `2` | Значение `InputRawEvent.type` для относительных осей |
| `INPUT_RAW_EVENT_ABS` | `3` | Значение `InputRawEvent.type` для абсолютных осей |
| `INPUT_RAW_SYN_REPORT` | `0` | Значение `code` для границы набора событий |
| `INPUT_RAW_SYN_DROPPED` | `3` | Значение `code` при потере части событий |
| `INPUT_RAW_CAP_KEY` | `1` | Устройство поддерживает события `EV_KEY` |
| `INPUT_RAW_CAP_RELATIVE` | `2` | Устройство поддерживает события `EV_REL` |
| `INPUT_RAW_CAP_ABSOLUTE` | `4` | Устройство поддерживает события `EV_ABS` |
| `INPUT_RAW_CAP_KEYBOARD_KEYS` | `8` | Среди кодов `EV_KEY` есть код из диапазона `KEY_*`; такие коды есть и выше `BTN_MISC` (`0x100`) |
| `INPUT_RAW_CAP_POINTER_BUTTONS` | `16` | Среди кодов `EV_KEY` есть код из диапазона `BTN_*`; коды выше `BTN_MISC` не обязательно являются кнопками |

Два последних флага читаются через `EVIOCGBIT(EV_KEY, len)` и имеют смысл
только вместе с `INPUT_RAW_CAP_KEY`: одного `EV_KEY` недостаточно, потому что
мышь сообщает кнопки тоже через `EV_KEY`.
Диапазоны `BTN_*` в текущей реализации: `0x100–0x15f`, `0x220–0x227`
и `0x2c0–0x2e7`. Остальные распознаваемые коды `EV_KEY` от `1` до `0x2ff`
относятся к клавишам.

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `input_raw_device_open(config, out_device)` | `const InputRawDeviceConfig *config`, `InputRawDeviceHandle *out_device` | `ErrorCode` | Открывает Linux input device в неблокирующем режиме и получает его метаданные |
| `input_raw_device_get_fd(device, out_fd)` | `InputRawDeviceHandle device`, `i32 *out_fd` | `ErrorCode` | Возвращает невладеющий файловый дескриптор устройства |
| `input_raw_device_get_info(device, out_info)` | `InputRawDeviceHandle device`, `InputRawDeviceInfo *out_info` | `ErrorCode` | Возвращает идентификаторы и raw-capabilities, определённые при открытии |
| `input_raw_device_read(device, out_events, events_capacity, out_status, out_count)` | `InputRawDeviceHandle device`, `InputRawEvent *out_events`, `usize events_capacity`, `InputRawReadStatus *out_status`, `usize *out_count` | `ErrorCode` | Читает до `events_capacity` сырых событий; при отсутствии данных возвращает `NoEvents`, при отключении устройства — `DeviceLost` |
| `input_raw_device_close(device)` | `InputRawDeviceHandle *device` | `ErrorCode` | Закрывает устройство, освобождает внутреннее состояние и записывает `NULL` |
