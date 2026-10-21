## Описание

Специальная библиотека для захвата сырых кадров с одной камеры Linux V4L2.
Библиотека открывает явно выбранное устройство `/dev/video*`, настраивает
поток, управляет буферами V4L2 и передаёт готовые кадры `camserv` без лишнего
копирования памяти.

## Имя библиотеки

`special/camera`

## Контракты

1. Библиотека является техническим адаптером V4L2 и используется `camserv`.
   Она не использует IPC, не предоставляет API приложениям и не принимает
   решения о доступе к камере.
2. Библиотека работает только с явно переданным путём `/dev/video*`, который
   поддерживает V4L2 video capture. Обнаружение камер, выбор устройства,
   permissions и повторное подключение являются ответственностью `camserv`.
3. Библиотека использует `libcore`, `libmem`, `libstr`, `libtime`,
   `libeventloop` и `sys/v4l2`. Доступ к Linux V4L2 ABI, `ioctl()` и
   MMAP-буферам выполняется только через `sys/v4l2`.
4. Все операции возвращают `ErrorCode` из `libcore`. `CORE_OK` является
   единственным успешным результатом.
5. Значения параметров с префиксом `out_` определены только при возврате
   `CORE_OK`, кроме `out_status` в `camera_dequeue_frame()`: оно определено
   при `CORE_OK`, а `out_frame` определён только при статусе `FrameReady`.
6. `CameraHandle` создаётся через `camera_open()` и закрывается через
   `camera_close()` либо аварийно через `camera_abort()`. Библиотека владеет файловым дескриптором камеры;
   дескриптор, полученный через `camera_get_fd()`, является невладеющим и не
   закрывается вызывающим кодом.
7. Камера открывается с `O_NONBLOCK` и `O_CLOEXEC`. Операция
   `camera_dequeue_frame()` не ожидает кадр: при отсутствии готового кадра она
   возвращает статус `NoFrame`.
8. `camserv` регистрирует дескриптор из `camera_get_fd()` в `sys/eventloop`. При
   `EVENT_READABLE` он вызывает `camera_dequeue_frame()`. При `EVENT_HANGUP`
   или `EVENT_ERROR` он повторяет чтение для получения `DeviceLost`, затем
   закрывает handle.
9. Один `CameraHandle` принадлежит создавшему его потоку. Одновременные
   операции с одним handle не допускаются; синхронизацию между потоками
   выполняет `camserv` через `libsync`.
10. Библиотека использует V4L2 streaming I/O с MMAP-буферами. `CameraFrame`
    не владеет памятью: `CameraFrame.data` указывает на отображённый буфер
    драйвера и действителен только до `camera_frame_release()`. Все поля кадра
    возвращаются без изменений; проверяются номер буфера, адрес и размер данных,
    время и sequence. Чужой, изменённый или устаревший кадр отклоняется.
11. Каждый кадр со статусом `FrameReady` должен быть передан ровно один раз в
    `camera_frame_release()`. Пока камера находится в `Streaming` и хотя бы
    один кадр не возвращён драйверу, `camera_stop()`, `camera_configure()` и
    `camera_close()` возвращают `CORE_INVALID_STATE`. При переходе в
    `DeviceLost` все выданные кадры становятся недействительными, а
    `camera_close()` допускается без их возврата.
    Если `camera_frame_release()` завершился ошибкой и кадр остался выданным,
    `camera_abort()` закрывает FD до удаления MMAP, делает все выданные кадры
    недействительными и освобождает handle.
12. Библиотека не декодирует `MJPEG`, не преобразует pixel format, не меняет
    размер кадра и не применяет фильтры. `camserv` передаёт или обрабатывает
    байты кадра в исходном формате.
13. `camera_configure()` выбирает только конфигурацию, явно переданную
    `camserv`. Библиотека не ищет «лучшее» разрешение, частоту кадров или
    формат; фактически применённые значения, включая `max_frame_bytes`,
    возвращаются через `out_actual_config`. В запросе `max_frame_bytes` равен
    `0`; в фактической конфигурации это положительный размер MMAP-буфера,
    полученный от `sys/v4l2`.
14. Библиотека поддерживает только поток захвата одного изображения V4L2.
    Запись видеофайла, звук, камера-контролы, одновременная работа с несколькими
    камерами и UI не входят в её ответственность.
15. Версия v1 поддерживает только дискретные комбинации pixel format, размера
    и FPS из `sys/v4l2`. Если драйвер сообщает непрерывный или пошаговый
    диапазон параметров, `camera_get_configurations()` возвращает
    `CORE_UNSUPPORTED`. Каждый элемент списка содержит положительный
    `max_frame_bytes`, полученный для этой комбинации. Дробную дискретную
    частоту `sys/v4l2` представляет точно, но целочисленный FPS в ABI
    `camlib` её не вмещает: при перечислении такие комбинации пропускаются,
    а если представимых комбинаций нет, возвращается `CORE_UNSUPPORTED`.
    Применение дробной частоты возвращает `CORE_UNSUPPORTED`; округления нет.

### Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `libcore` | Базовые типы, `Bytes`, `ErrorCode` и проверка размеров буферов |
| `libmem` | `CameraHandle`, таблица MMAP-буферов и список конфигураций |
| `libstr` | UTF-8 путь к устройству камеры |
| `libtime` | Тип `MonotonicTime` для метки кадра из V4L2 |
| `libeventloop` | Не является зависимостью линковки; `camserv` использует файловый дескриптор камеры в event loop |
| `sys/v4l2` | Доступ к V4L2-устройству, настройка capture-потока и MMAP-буферы |

### Состояния камеры

| Состояние | Допустимые операции | Назначение |
| --- | --- | --- |
| `Open` | `camera_get_fd()`, `camera_get_configurations()`, `camera_configure()`, `camera_close()` | Устройство открыто, но поток кадров не настроен |
| `Configured` | `camera_get_fd()`, `camera_get_configurations()`, `camera_configure()`, `camera_start()`, `camera_close()` | Формат выбран, но захват ещё не запущен |
| `Streaming` | `camera_get_fd()`, `camera_dequeue_frame()`, `camera_frame_release()`, `camera_stop()` | V4L2 получает кадры и отдаёт готовые буферы |
| `DeviceLost` | `camera_close()` | Устройство отключено или драйвер прекратил захват |

Операция, не допустимая в текущем состоянии, возвращает
`CORE_INVALID_STATE`. В частности, `camera_configure()` и `camera_close()`
запрещены в `Streaming`: иначе MMAP-буферы были бы освобождены без
предварительного `STREAMOFF`.

### Поддерживаемые форматы

| `CameraPixelFormat` | V4L2 FourCC | Представление данных | Назначение |
| --- | --- | --- | --- |
| `Yuyv` | `YUYV` | Несжатые YUV 4:2:2 пиксели | Базовый совместимый формат USB-вебкамер |
| `Mjpeg` | `MJPG` | Один сжатый JPEG-кадр | Компактная передача кадров с веб-камеры |
| `Nv12` | `NV12` | Несжатые YUV 4:2:0, две плоскости | Формат для последующего видео-конвейера |

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| --- | --- | --- | --- |
| `CameraHandle` | Непрозрачный указатель: `struct CameraHandle *` | `NULL` или открытая камера | Контекст одного V4L2-устройства |
| `CameraConfig` | Структура: `const String *path` | Непустой UTF-8 путь `/dev/video*` без `NUL` | Конфигурация открытия камеры |
| `CameraPixelFormat` | Перечисление | `Yuyv`, `Mjpeg`, `Nv12` | Формат пикселей потока V4L2 |
| `CameraStreamConfig` | Структура: `CameraPixelFormat format`, `u32 width`, `u32 height`, `u32 frames_per_second`, `u32 max_frame_bytes` | Формат, размер и FPS поддерживаются устройством; `0` только в запросе, положительный размер только в фактической конфигурации | Параметры потока и максимальный размер одного кадра |
| `CameraConfigurationList` | Структура: `CameraStreamConfig *items`, `usize count` | Владеющий массив дискретных конфигураций длиной от `0` | Набор конфигураций, поддерживаемых камерой |
| `CameraFrame` | Структура: `u32 buffer_id`, `Bytes data`, `MonotonicTime timestamp`, `u64 sequence` | Только кадр, выданный текущим `CameraHandle` | Невладеющее представление готового V4L2-буфера |
| `CameraReadStatus` | Перечисление | `FrameReady`, `NoFrame`, `DeviceLost` | Результат попытки получить следующий кадр |

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `CAMERA_BUFFER_COUNT` | `4` | Предпочтительное число V4L2 MMAP-буферов для одного потока |
| `CAMERA_MIN_WIDTH` | `1` | Минимальная допустимая ширина кадра |
| `CAMERA_MIN_HEIGHT` | `1` | Минимальная допустимая высота кадра |
| `CAMERA_MIN_FRAMES_PER_SECOND` | `1` | Минимальная допустимая частота кадров |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| --- | --- | --- | --- |
| `camera_open(config, out_camera)` | `const CameraConfig *config`, `CameraHandle *out_camera` | `ErrorCode` | Открывает V4L2 capture-устройство неблокирующе и создаёт handle в состоянии `Open` |
| `camera_get_fd(camera, out_fd)` | `CameraHandle camera`, `i32 *out_fd` | `ErrorCode` | Возвращает невладеющий файловый дескриптор камеры для регистрации в event loop |
| `camera_get_configurations(camera, out_configurations)` | `CameraHandle camera`, `CameraConfigurationList *out_configurations` | `ErrorCode` | Возвращает владеющий список дискретных комбинаций формата, размера и FPS |
| `camera_configuration_list_destroy(configurations)` | `CameraConfigurationList *configurations` | `ErrorCode` | Освобождает список конфигураций через `libmem` и делает его недействительным |
| `camera_configure(camera, requested_config, out_actual_config)` | `CameraHandle camera`, `const CameraStreamConfig *requested_config`, `CameraStreamConfig *out_actual_config` | `ErrorCode` | Настраивает V4L2-поток и MMAP-буферы; возвращает фактически применённую конфигурацию |
| `camera_start(camera)` | `CameraHandle camera` | `ErrorCode` | Ставит все внутренние буферы в очередь V4L2 и запускает захват кадров |
| `camera_dequeue_frame(camera, out_status, out_frame)` | `CameraHandle camera`, `CameraReadStatus *out_status`, `CameraFrame *out_frame` | `ErrorCode` | Извлекает один готовый V4L2-буфер; при отсутствии кадра возвращает `NoFrame`, при отключении — `DeviceLost` |
| `camera_frame_release(camera, frame)` | `CameraHandle camera`, `CameraFrame *frame` | `ErrorCode` | Возвращает ранее полученный буфер драйверу V4L2 и делает `CameraFrame` недействительным |
| `camera_stop(camera)` | `CameraHandle camera` | `ErrorCode` | Останавливает V4L2 streaming и освобождает MMAP-буферы; при успехе камера возвращается в `Open` и требует новой настройки перед запуском. При ошибке остановки переходит в `DeviceLost`: разрешено только закрытие |
| `camera_close(camera)` | `CameraHandle *camera` | `ErrorCode` | Закрывает камеру, освобождает все внутренние ресурсы и записывает `NULL` |
| `camera_abort(camera)` | `CameraHandle *camera` | `ErrorCode` | Аварийно закрывает камеру с невозвращённым кадром; все виды на кадры становятся недействительными |

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Передан `NULL`, пустой путь, нулевой размер/FPS или значение вне контракта операции |
| `CORE_INVALID_PATH` | Путь не является допустимым путём устройства V4L2 |
| `CORE_INVALID_STATE` | Операция вызвана в недопустимом состоянии или существует не возвращённый `CameraFrame` |
| `CORE_NOT_FOUND` | Устройство по указанному пути не существует |
| `CORE_PERMISSION_DENIED` | Процесс не имеет прав на открытие камеры |
| `CORE_OUT_OF_MEMORY` | Невозможно выделить handle, таблицу буферов или список конфигураций |
| `CORE_IO_ERROR` | Ошибка драйвера, повреждённый V4L2-ответ или ошибка захвата, не означающая отключение устройства |
| `CORE_UNSUPPORTED` | Устройство не поддерживает V4L2 video capture, MMAP streaming или запрошенную конфигурацию |
