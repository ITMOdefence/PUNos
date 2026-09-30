## Описание

Системная библиотека для доступа к Linux V4L2 video capture-устройствам.
Инкапсулирует V4L2 ABI, настройку видеопотока и MMAP-буферы, не определяя
политику использования камеры.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libv4l2`. Готовы:

- публичный заголовок `<sys/v4l2.h>` со всеми типами и константами;
- статическая библиотека `libpunos_v4l2.a`: V4L2 ABI (структуры
  `v4l2_capability/fmtdesc/frmsizeenum/frmivalenum/format/pix_format/
  requestbuffers/buffer/streamparm` и номера `VIDIOC_*`) написан вручную (в
  PUNos нет `<linux/videodev2.h>`) и сверен по `sizeof`/`offsetof` с
  `/usr/include/linux/videodev2.h` на хосте сборки — размеры структур
  напрямую влияют на закодированный номер ioctl, поэтому сверялись не по
  тексту, а числами;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест (`tests/v4l2_runtime.c`), адаптирующийся к машине: без
  реального `/dev/video*` проверяет только `CORE_NOT_FOUND` и все
  `NULL`-аргументные пути; на реальном или виртуальном (`v4l2loopback`)
  устройстве проверяет полный цикл `open → get_capture_configurations →
  configure_capture → start_capture → dequeue/queue_buffer → stop_capture →
  close`. На машине, где это делалось, `/dev/video*` не оказалось, поэтому
  выполнялась только первая часть; вторая проверена конструктивно (сверка
  ABI, соответствие последовательности ioctl документированному V4L2
  streaming I/O). Выполняется на хосте командой
  `make -C System/SystemLibs/libv4l2 check` и внутри PUNos в QEMU как
  `/punos/bin/libv4l2-selftest`.

Библиотека вызывает только `open()`, `close()`, `ioctl()`, `mmap()` и
`munmap()` (контракт 1). Решения, принятые при реализации, описаны в
`System/SystemLibs/libv4l2/README.md`.

## Имя библиотеки

`sys/v4l2`

## Контракты

1. Библиотека использует только Syscall Wrappers `open()`, `close()`,
   `ioctl()`, `mmap()` и `munmap()`, а также типы и `ErrorCode` из `libcore`,
   память из `libmem`, строки из `libstr` и монотонное время из `libtime`.
2. Библиотека поддерживает только Linux V4L2 video capture с streaming I/O и
   MMAP-буферами. Video output, metadata capture, read/write I/O и
   multi-planar formats не поддерживаются.
3. Все операции возвращают `ErrorCode` из `libcore`.
4. Значения результата передаются через параметры с префиксом `out_`.
   При возврате ошибки значения `out_*` не определены, кроме `out_status` в
   `v4l2_dequeue_buffer()`: он определён при `CORE_OK`, а `out_buffer`
   определён только при статусе `BufferReady`.
5. `V4l2DeviceHandle` является непрозрачным указателем. Созданный handle
   должен быть закрыт ровно один раз через `v4l2_close()`.
6. Библиотека владеет файловым дескриптором устройства. Дескриптор,
   возвращённый `v4l2_get_fd()`, является невладеющим; вызывающий код не
   закрывает его.
7. Устройство открывается с `O_NONBLOCK` и `O_CLOEXEC`. Операция
   `v4l2_dequeue_buffer()` не ожидает кадр и возвращает `NoBuffer`, если
   драйвер не подготовил буфер.
8. `V4l2Buffer.data` является невладеющим указателем на MMAP-буфер драйвера.
   Он действителен только до успешного `v4l2_queue_buffer()`. Каждый буфер со
   статусом `BufferReady` должен быть возвращён драйверу ровно один раз.
9. Пока существует хотя бы один не возвращённый `V4l2Buffer`, операции
   `v4l2_stop_capture()`, `v4l2_configure_capture()` и `v4l2_close()`
   возвращают `CORE_INVALID_STATE`.
10. При `DeviceLost` все ранее выданные `V4l2Buffer` становятся
    недействительными. В этом состоянии допускается только `v4l2_close()`.
11. `V4l2StreamConfig` задаёт точную запрошенную конфигурацию. Поле
    `max_frame_bytes` в запросе равно `0`; в конфигурации, фактически
    применённой драйвером, оно равно размеру одного MMAP-буфера (`sizeimage`).
    Библиотека не выбирает предпочтительный формат, размер или FPS.
12. Список конфигураций содержит только дискретные комбинации pixel format,
    размера и FPS. Если драйвер сообщает непрерывный или пошаговый диапазон,
    библиотека возвращает `CORE_UNSUPPORTED`. Каждый элемент списка содержит
    положительный `max_frame_bytes`, полученный для этой комбинации через
    V4L2 `sizeimage`; если его нельзя определить без запуска capture, операция
    также возвращает `CORE_UNSUPPORTED`.
13. Библиотека не интерпретирует пиксели, не декодирует сжатые форматы и не
    использует IPC, event loop, permissions или UI.
14. `V4l2Buffer.timestamp` получается через `time_monotonic_now()` сразу после
    успешного извлечения буфера. Метка не является исходной меткой драйвера
    V4L2 и применяется только для упорядочивания и измерения интервалов.
15. Указатели на структуры V4L2 передаются третьим аргументом `ioctl()` только
    после явного преобразования через `uintptr_t` в `unsigned long`.
    Библиотека не предполагает, что третий аргумент wrapper имеет тип
    указателя.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `String` | Тип из `libstr` | UTF-8 строка без `NUL` | Путь к V4L2-устройству |
| `Bytes` | Тип из `libcore`: `u8 *data`, `usize size` | Невладеющий буфер байтов | Отображённые данные одного V4L2-буфера |
| `MonotonicTime` | Тип из `libtime` | Момент монотонных часов в миллисекундах | Метка времени V4L2-буфера |
| `V4l2DeviceHandle` | Непрозрачный указатель: `struct V4l2DeviceHandle *` | `NULL` или открытое устройство | Контекст V4L2 video capture-устройства |
| `V4l2DeviceConfig` | Структура: `const String *path` | Непустой путь `/dev/video*` без `NUL` | Конфигурация открытия устройства |
| `V4l2PixelFormat` | `u32` | FourCC, сообщаемый драйвером V4L2 | Формат пикселей без интерпретации библиотекой |
| `V4l2StreamConfig` | Структура: `V4l2PixelFormat pixel_format`, `u32 width`, `u32 height`, `u32 frames_per_second`, `u32 max_frame_bytes` | Формат, размер и FPS поддерживаются устройством; `0` только в запросе, положительный размер только в фактической конфигурации | Запрошенные или применённые параметры потока и верхняя граница `bytesused` |
| `V4l2StreamConfigList` | Структура: `V4l2StreamConfig *items`, `usize count` | Владеющий массив длиной от `0` | Дискретные конфигурации capture-потока |
| `V4l2Buffer` | Структура: `u32 index`, `Bytes data`, `MonotonicTime timestamp`, `u64 sequence` | Буфер, выданный текущим `V4l2DeviceHandle` | Невладеющее представление готового MMAP-буфера |
| `V4l2DequeueStatus` | Перечисление | `BufferReady`, `NoBuffer`, `DeviceLost` | Результат неблокирующей попытки получить буфер |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `V4L2_BUFFER_COUNT` | Предпочтительное число MMAP-буферов capture-потока: `4` |
| `V4L2_MIN_WIDTH` | Минимальная допустимая ширина кадра: `1` |
| `V4L2_MIN_HEIGHT` | Минимальная допустимая высота кадра: `1` |
| `V4L2_MIN_FRAMES_PER_SECOND` | Минимальная допустимая частота кадров: `1` |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `v4l2_open(config, out_device)` | `const V4l2DeviceConfig *config`, `V4l2DeviceHandle *out_device` | `ErrorCode` | Открывает V4L2 video capture-устройство в неблокирующем режиме |
| `v4l2_get_fd(device, out_fd)` | `V4l2DeviceHandle device`, `i32 *out_fd` | `ErrorCode` | Возвращает невладеющий файловый дескриптор V4L2-устройства |
| `v4l2_get_capture_configurations(device, out_configurations)` | `V4l2DeviceHandle device`, `V4l2StreamConfigList *out_configurations` | `ErrorCode` | Возвращает владеющий список дискретных конфигураций, поддерживаемых драйвером |
| `v4l2_stream_config_list_destroy(configurations)` | `V4l2StreamConfigList *configurations` | `ErrorCode` | Освобождает список конфигураций через `libmem` и делает его недействительным |
| `v4l2_configure_capture(device, requested_config, out_actual_config)` | `V4l2DeviceHandle device`, `const V4l2StreamConfig *requested_config`, `V4l2StreamConfig *out_actual_config` | `ErrorCode` | Настраивает video capture format и создаёт MMAP-буферы; возвращает применённую конфигурацию |
| `v4l2_start_capture(device)` | `V4l2DeviceHandle device` | `ErrorCode` | Ставит все MMAP-буферы в очередь драйвера и запускает streaming |
| `v4l2_dequeue_buffer(device, out_status, out_buffer)` | `V4l2DeviceHandle device`, `V4l2DequeueStatus *out_status`, `V4l2Buffer *out_buffer` | `ErrorCode` | Извлекает один готовый буфер; при отсутствии кадра возвращает `NoBuffer`, при отключении устройства — `DeviceLost` |
| `v4l2_queue_buffer(device, buffer)` | `V4l2DeviceHandle device`, `V4l2Buffer *buffer` | `ErrorCode` | Возвращает ранее извлечённый MMAP-буфер драйверу и делает структуру недействительной |
| `v4l2_stop_capture(device)` | `V4l2DeviceHandle device` | `ErrorCode` | Останавливает streaming и освобождает MMAP-буферы |
| `v4l2_close(device)` | `V4l2DeviceHandle *device` | `ErrorCode` | Закрывает устройство, освобождает внутренние ресурсы и записывает `NULL` |
