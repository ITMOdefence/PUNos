# libgraphics

## Назначение

`libgraphics` задаёт публичную границу между GUI display-слоем и конкретным
графическим устройством. Первая реализация предоставляет один виртуальный
мобильный output и программный framebuffer. DRM/KMS и backend реального ARM64
устройства позднее реализуют тот же публичный ABI.

## Контракты

1. Backend работает неблокирующе и публикует события через невладеющий
   `eventfd`, доступный через `graphics_device_get_fd()`.
2. Импортированный frame принимает переданную копию FD во владение только при
   `CORE_OK`; mapping создаётся read-only и non-executable.
3. После `graphics_output_present()` frame нельзя уничтожать до получения
   события с тем же `present_id`.
4. Программный backend копирует видимую часть каждой строки в собственный
   framebuffer. Padding после `width * 4` не копируется.
5. Одновременно в очереди находится не более двух completion events.
6. Все размеры, stride, формат, размер backing storage и состояние frame
   проверяются до постановки кадра.

## Публичный заголовок

`<sys/graphics.h>`

Основные операции: `graphics_device_open`, `graphics_device_get_output`,
`graphics_output_configure`, `graphics_frame_import`,
`graphics_output_present`, `graphics_device_next_event`,
`graphics_frame_destroy`, `graphics_device_close`.
