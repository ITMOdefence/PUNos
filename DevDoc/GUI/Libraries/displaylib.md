## Описание

Специальная библиотека для доступа к одному графическому backend и его
физическим выходам. Библиотека перечисляет дисплеи и режимы, применяет
конфигурацию, импортирует готовые кадровые буферы и передаёт их backend для
показа. Единственным потребителем библиотеки является `displayserv`.

## Имя библиотеки

`special/display`

## Контракты

1. Библиотека является техническим адаптером графического backend. Она не
   использует IPC, не предоставляет API приложениям и не знает об окнах,
   поверхностях, фокусе или политике размещения.
2. Обнаружение доступного backend и выбор пути устройства выполняются по
   `DisplayBackendConfig`, переданному `displayserv`. Автоматическая смена
   backend во время работы в v1 не выполняется.
3. Библиотека использует `libcore`, `libmem`, `libstr`, `libeventloop` и
   публичный системный graphics ABI. Прямые syscall и внутренние заголовки
   драйвера за пределами backend-модуля запрещены.
4. Все операции возвращают `ErrorCode` из `libcore`. `CORE_OK` является
   единственным успешным результатом.
5. Значения параметров с префиксом `out_` определены только при возврате
   `CORE_OK`, кроме `out_status` в `display_backend_next_event()`: событие
   определено только при статусе `EventReady`.
6. `DisplayBackendHandle` создаётся через `display_backend_open()` и закрывается
   ровно один раз через `display_backend_close()`. Библиотека владеет FD и
   отображениями backend.
7. FD из `display_backend_get_fd()` является невладеющим. `displayserv`
   регистрирует его в event loop, но не закрывает напрямую.
8. Backend открывается неблокирующе и с `O_CLOEXEC`. Получение событий и
   постановка кадра не блокируют поток; временная невозможность операции
   возвращает `CORE_WOULD_BLOCK` или статус `NoEvent`.
9. `DisplayOutputHandle` принадлежит backend. Он действителен до события
   `OutputRemoved` или закрытия backend и не освобождается отдельно.
10. `DisplayOutputInfoList`, возвращённый перечислением, владеет массивом и
    вложенными списками режимов. Вызывающий код освобождает его ровно один раз
    через `display_output_info_list_destroy()`.
11. `DisplayFrameHandle` создаётся через `display_frame_import()`. Библиотека
    принимает во владение переданную ей копию FD только при `CORE_OK`.
    При ошибке владение FD остаётся у вызывающего кода.
12. После успешного `display_output_present()` frame занят до события
    `PresentComplete` или `PresentFailed` с тем же `present_id`. Уничтожать его
    до события нельзя.
13. Один backend и все полученные из него handle принадлежат одному потоку.
    Одновременные операции не допускаются; сериализацию обеспечивает
    `displayserv`.
14. `display_output_configure()` инвалидирует старое поколение конфигурации.
    Кадр для прежнего размера или поколения не передаётся на present.
15. Библиотека проверяет размеры, stride, формат и размер backing storage до
    отображения. Исполняемое отображение памяти не создаётся.
16. Hotplug, если он поддерживается backend, передаётся событиями. Политику
    выбора режима и восстановление после отключения определяет `displayserv`.

### Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `libcore` | Базовые типы, `ErrorCode`, проверки диапазонов и переполнения |
| `libmem` | Handle, списки выходов и внутренние таблицы imported frames |
| `libstr` | UTF-8 имя backend и путь устройства |
| `libeventloop` | Не является зависимостью линковки; сервис использует возвращённый FD в event loop |
| System graphics ABI | Открытие устройства, режимы, mmap кадров и backend events |

### Состояния backend

| Состояние | Допустимые операции | Переходы |
| --- | --- | --- |
| `Open` | enumerate, configure, import, present, next_event | `Lost`, `Closed` |
| `Lost` | next_event, close | `Closed` |
| `Closed` | Нет | Нет |

### Состояния кадра

| Состояние | Допустимые операции | Переходы |
| --- | --- | --- |
| `Imported` | present, destroy | `Queued`, `Destroyed` |
| `Queued` | ожидание события | `Presented`, `Failed` |
| `Presented` | present, destroy | `Queued`, `Destroyed` |
| `Failed` | present, destroy | `Queued`, `Destroyed` |
| `Destroyed` | Нет | Нет |

### Поддерживаемые форматы v1

| Формат | Представление | Назначение |
| --- | --- | --- |
| `DisplayPixelXrgb8888` | B, G, R, X; 32 бита | Непрозрачный кадр |
| `DisplayPixelArgb8888` | B, G, R, A; premultiplied alpha | Кадр с alpha, если backend поддерживает |

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| --- | --- | --- | --- |
| `DisplayBackendHandle` | Непрозрачный указатель: `struct DisplayBackendHandle *` | `NULL` или открытый backend | Контекст одного graphics backend |
| `DisplayOutputHandle` | Непрозрачный невладеющий указатель | Выход текущего backend | Физический или виртуальный дисплей |
| `DisplayFrameHandle` | Непрозрачный указатель | Imported frame текущего backend | Кадр, пригодный для present |
| `DisplayBackendConfig` | `const String *backend_name`, `const String *device_path` | Корректные UTF-8 строки; путь может отсутствовать для default backend | Конфигурация открытия backend |
| `DisplayMode` | `u32 width_px`, `u32 height_px`, `u32 refresh_millihz` | Ненулевые размеры и частота | Один поддерживаемый режим |
| `DisplayOutputInfo` | `u64 backend_output_id`, `String name`, `bool connected`, `bool enabled`, физический размер, текущий режим и список режимов | Снимок одного выхода | Метаданные дисплея |
| `DisplayOutputInfoList` | `DisplayOutputInfo *items`, `usize count` | Владеющий массив от `0` элементов | Результат перечисления выходов |
| `DisplayOutputConfig` | `bool enabled`, `DisplayMode mode`, `u32 orientation` | Режим из списка выхода | Запрашиваемая конфигурация |
| `DisplayFrameDescriptor` | `u32 width`, `u32 height`, `u32 stride`, `DisplayPixelFormat format`, `u64 byte_size` | Валидированный размер backing storage | Метаданные imported frame |
| `DisplayDamage` | `const GuiRect *rects`, `usize count` | Не более `DISPLAY_DAMAGE_MAX_RECTS` | Изменившиеся области кадра |
| `DisplayBackendEvent` | Tagged union | Output и present events | Одно событие backend |
| `DisplayEventStatus` | Перечисление | `EventReady`, `NoEvent`, `BackendLost` | Результат чтения события |

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `DISPLAY_DAMAGE_MAX_RECTS` | `64` | Максимум отдельных damage-прямоугольников |
| `DISPLAY_PRESENT_QUEUE_DEPTH` | `2` | Максимум кадров в backend-очереди одного выхода |
| `DISPLAY_ORIENTATION_NORMAL` | `0` | Вывод без поворота |
| `DISPLAY_ORIENTATION_90` | `1` | Поворот по часовой стрелке на 90 градусов |
| `DISPLAY_ORIENTATION_180` | `2` | Поворот на 180 градусов |
| `DISPLAY_ORIENTATION_270` | `3` | Поворот по часовой стрелке на 270 градусов |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| --- | --- | --- | --- |
| `display_backend_open(config, out_backend)` | `const DisplayBackendConfig *config`, `DisplayBackendHandle *out_backend` | `ErrorCode` | Открывает backend неблокирующе и создаёт handle в состоянии `Open` |
| `display_backend_get_fd(backend, out_fd)` | `DisplayBackendHandle backend`, `i32 *out_fd` | `ErrorCode` | Возвращает невладеющий FD событий для event loop |
| `display_backend_list_outputs(backend, out_outputs)` | `DisplayBackendHandle backend`, `DisplayOutputInfoList *out_outputs` | `ErrorCode` | Возвращает владеющий снимок выходов и режимов |
| `display_output_info_list_destroy(outputs)` | `DisplayOutputInfoList *outputs` | `ErrorCode` | Освобождает список и делает его недействительным |
| `display_output_configure(backend, output_id, config)` | `DisplayBackendHandle backend`, `u64 output_id`, `const DisplayOutputConfig *config` | `ErrorCode` | Атомарно применяет поддерживаемую конфигурацию выхода |
| `display_frame_import(backend, descriptor, fd, out_frame)` | `DisplayBackendHandle backend`, `const DisplayFrameDescriptor *descriptor`, `i32 fd`, `DisplayFrameHandle *out_frame` | `ErrorCode` | Проверяет metadata и принимает копию FD во владение при успехе |
| `display_output_present(backend, output_id, frame, damage, present_id)` | backend, output id, frame, damage, ненулевой `u64 present_id` | `ErrorCode` | Неблокирующе ставит кадр в очередь выбранного выхода |
| `display_backend_next_event(backend, out_status, out_event)` | `DisplayBackendHandle backend`, `DisplayEventStatus *out_status`, `DisplayBackendEvent *out_event` | `ErrorCode` | Возвращает следующее hotplug/present событие без ожидания |
| `display_frame_destroy(frame)` | `DisplayFrameHandle *frame` | `ErrorCode` | Закрывает owned FD, освобождает отображение и записывает `NULL` |
| `display_backend_close(backend)` | `DisplayBackendHandle *backend` | `ErrorCode` | Закрывает backend после освобождения кадров и записывает `NULL` |

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Передан `NULL`, нулевой размер, неверный stride, orientation или формат |
| `CORE_INVALID_PATH` | Путь backend-устройства имеет недопустимый формат |
| `CORE_INVALID_STATE` | Операция вызвана для lost/closed backend или queued frame |
| `CORE_NOT_FOUND` | Backend-устройство или output id не существует |
| `CORE_PERMISSION_DENIED` | Процесс не имеет прав на графическое устройство |
| `CORE_OUT_OF_MEMORY` | Нельзя выделить handle, список, таблицу или отображение |
| `CORE_OVERFLOW` | Размер кадра или вычисление stride/height выходит за диапазон |
| `CORE_WOULD_BLOCK` | Backend временно не принимает новый present или событий нет |
| `CORE_IO_ERROR` | Ошибка драйвера, mmap, configure или present |
| `CORE_UNSUPPORTED` | Backend, режим, формат или обязательная операция не поддерживается |
