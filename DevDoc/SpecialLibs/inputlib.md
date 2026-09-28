## Описание

Специальная библиотека для доступа к отдельному устройству Linux input
subsystem через `/dev/input/event*`. Библиотека читает физические события,
преобразует их в единый формат `InputEvent` и объединяет их в кадры,
завершаемые `SYN_REPORT`.

## Имя библиотеки

`special/input`

## Контракты

1. Библиотека является техническим адаптером Linux input subsystem и
   используется `IOserv`. Она не предоставляет API приложениям, не использует
   IPC и не маршрутизирует события окнам.
2. Библиотека работает только с явно переданным путём устройства вида
   `/dev/input/event*`. Обнаружение устройств, выбор нужного устройства и
   permissions являются ответственностью `IOserv`.
3. Библиотека использует `libcore`, `libmem`, `libstr`, `libeventloop` и
   `sys/input-device`. Доступ к Linux evdev ABI, чтению и `ioctl()` выполняется
   только через `sys/input-device`.
4. Все операции возвращают `ErrorCode` из `libcore`. `CORE_OK` является
   единственным успешным результатом.
5. Значения параметров с префиксом `out_` определены только при возврате
   `CORE_OK`, кроме `out_status` в `input_device_read_frame()`: оно определено
   при `CORE_OK`, а `out_frame` определён только при статусе `FrameReady`.
6. `InputDeviceHandle` создаётся через `input_device_open()` и закрывается
   ровно один раз через `input_device_close()`. Библиотека владеет файловым
   дескриптором устройства; дескриптор из `input_device_get_fd()` является
   невладеющим и не закрывается вызывающим кодом.
7. Устройство открывается с `O_NONBLOCK` и `O_CLOEXEC`. Вызов
   `input_device_read_frame()` не ожидает данные от ядра: при отсутствии
   полного кадра он возвращает статус `NoFrame`.
8. Вызывающий код регистрирует дескриптор из `input_device_get_fd()` в
   `sys/eventloop`. При `EVENT_READABLE` допустимо вызвать
   `input_device_read_frame()`. `EVENT_HANGUP` или `EVENT_ERROR` требуют
   повторного вызова чтения для получения статуса `DeviceLost`, после чего
   handle должен быть закрыт.
9. Один `InputDeviceHandle` принадлежит создавшему его потоку. Одновременные
   операции с одним handle не допускаются; синхронизацию между потоками
   выполняет `IOserv` через `libsync`.
10. `InputFrame`, возвращённый через `input_device_read_frame()`, владеет
    массивом событий. Вызывающий код должен освободить его ровно один раз через
    `input_frame_destroy()`.
    У успешно возвращённого кадра `events != NULL` и `count >= 1`; значения
    `events = NULL` и `count = 0` допустимы только после уничтожения кадра.
11. События в кадре имеют тот же порядок, что и события Linux input subsystem.
    Обычный кадр завершается событием `SyncReport`.
12. Если ядро сообщает `SYN_DROPPED` или размер незавершённого кадра превышает
    `INPUT_FRAME_MAX_EVENTS`, библиотека возвращает кадр из одного события
    `SyncLost`. После него `IOserv` обязан отбросить собственное состояние
    нажатых клавиш, кнопок и касаний для данного устройства.
13. `InputEvent.code` и `InputEvent.value` сохраняют значения Linux input
    subsystem. Библиотека не преобразует physical key code в символ, не меняет
    координаты и не интерпретирует жесты.
14. Библиотека классифицирует только клавиатуру, относительный указатель,
    абсолютный указатель и синхронизацию. Остальные события возвращаются как
    `Raw`, чтобы `IOserv` мог явно решить, как их обработать.

### Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `libcore` | Базовые типы, `Bytes`, `bool`, `ErrorCode` и проверка размеров |
| `libmem` | `InputDeviceHandle`, внутренний буфер и массив событий `InputFrame` |
| `libstr` | UTF-8 путь к устройству |
| `libeventloop` | Не является зависимостью линковки; `IOserv` использует возвращённый файловый дескриптор в event loop |
| `sys/input-device` | Открытие устройства, raw-события, файловый дескриптор и метаданные evdev |

### Соответствие Linux input subsystem

| Linux событие | `InputEventKind` | Значение `code` | Значение `value` |
| --- | --- | --- | --- |
| `EV_KEY` | `Key` | Код `KEY_*` или `BTN_*` | `0` — отпускание, `1` — нажатие, `2` — автоповтор |
| `EV_REL` | `RelativeAxis` | Код `REL_*` | Относительное смещение оси |
| `EV_ABS` | `AbsoluteAxis` | Код `ABS_*` | Абсолютное значение оси устройства |
| `EV_SYN` с `SYN_REPORT` | `SyncReport` | `0` | `0` |
| `EV_SYN` с `SYN_DROPPED` | `SyncLost` | `SYN_DROPPED` (`3`) | `0` |
| Иные значения `EV_*` | `Raw` | Оригинальный код Linux | Оригинальное значение Linux |

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| --- | --- | --- | --- |
| `InputDeviceHandle` | Непрозрачный указатель: `struct InputDeviceHandle *` | `NULL` или открытое устройство | Контекст одного устройства `/dev/input/event*` |
| `InputDeviceConfig` | Структура: `const String *path` | Непустой UTF-8 путь к устройству без `NUL` | Конфигурация открытия устройства |
| `InputDeviceClass` | Битовая маска `u32` | Комбинация `INPUT_DEVICE_*` | Типы событий, доступные через устройство |
| `InputDeviceInfo` | Структура: `u16 bus`, `u16 vendor`, `u16 product`, `u16 version`, `InputDeviceClass classes` | Идентификаторы Linux input device и его классы | Базовые метаданные открытого устройства |
| `InputEventKind` | Перечисление | `Key`, `RelativeAxis`, `AbsoluteAxis`, `SyncReport`, `SyncLost`, `Raw` | Семантика физического события |
| `InputEvent` | Структура: `InputEventKind kind`, `u16 code`, `i32 value` | Одно нормализованное событие Linux input subsystem | Физическое событие устройства |
| `InputFrame` | Структура: `InputEvent *events`, `usize count` | Владеющий массив: `events != NULL`, `1 <= count <= INPUT_FRAME_MAX_EVENTS`; после destroy — `{ NULL, 0 }` | Атомарная последовательность событий устройства |
| `InputReadStatus` | Перечисление | `FrameReady`, `NoFrame`, `DeviceLost` | Результат попытки чтения следующего кадра |

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `INPUT_DEVICE_KEYBOARD` | `1` | Устройство сообщает события клавиш `EV_KEY` с кодами `KEY_*` |
| `INPUT_DEVICE_POINTER` | `2` | Устройство сообщает относительные оси `EV_REL` или кнопки указателя |
| `INPUT_DEVICE_TOUCH` | `4` | Устройство сообщает абсолютные оси `EV_ABS` для касаний или пера |
| `INPUT_DEVICE_UNKNOWN` | `0` | Класс устройства не удалось определить из поддерживаемых классов |
| `INPUT_FRAME_MAX_EVENTS` | `256` | Максимальное число событий одного кадра до `SyncReport` |

## Операции

| Операция                                                 | Аргументы                                                                          | Возвращаемое значение | Назначение                                                                                                                          |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------- | --------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `input_device_open(config, out_device)`                  | `const InputDeviceConfig *config`, `InputDeviceHandle *out_device`                 | `ErrorCode`           | Открывает устройство неблокирующе, получает его базовые возможности и создаёт handle                                                |
| `input_device_get_fd(device, out_fd)`                    | `InputDeviceHandle device`, `i32 *out_fd`                                          | `ErrorCode`           | Возвращает невладеющий файловый дескриптор устройства для регистрации в event loop                                                  |
| `input_device_get_info(device, out_info)`                | `InputDeviceHandle device`, `InputDeviceInfo *out_info`                            | `ErrorCode`           | Возвращает метаданные и классы событий, определённые при открытии устройства                                                        |
| `input_device_read_frame(device, out_status, out_frame)` | `InputDeviceHandle device`, `InputReadStatus *out_status`, `InputFrame *out_frame` | `ErrorCode`           | Считывает и нормализует следующий полный кадр; при отсутствии данных записывает `NoFrame`, при отключении устройства — `DeviceLost` |
| `input_frame_destroy(frame)`                             | `InputFrame *frame`                                                                | `ErrorCode`           | Освобождает массив событий кадра через `libmem` и делает структуру недействительной                                                 |
| `input_device_close(device)`                             | `InputDeviceHandle *device`                                                        | `ErrorCode`           | Закрывает файловый дескриптор устройства, освобождает внутреннее состояние и записывает `NULL`                                      |

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Передан `NULL`, пустой путь или значение вне контракта операции |
| `CORE_INVALID_PATH` | Путь не может использоваться как путь устройства input subsystem |
| `CORE_INVALID_STATE` | Операция вызвана с закрытым или недействительным handle |
| `CORE_NOT_FOUND` | Устройство по указанному пути не существует |
| `CORE_PERMISSION_DENIED` | Процесс не имеет прав на открытие устройства |
| `CORE_OUT_OF_MEMORY` | Невозможно выделить handle, внутренний буфер или кадр событий |
| `CORE_IO_ERROR` | Ошибка драйвера, повреждённая последовательность входных данных или ошибка чтения, не означающая отключение устройства |
| `CORE_UNSUPPORTED` | Устройство не поддерживает обязательный Linux input ABI |
