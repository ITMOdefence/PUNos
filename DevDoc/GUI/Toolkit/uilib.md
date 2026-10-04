## Описание

Клиентская библиотека графического приложения PUNos. Библиотека создаёт окна и
поверхности через `windowserv` и `compositorserv`, управляет пулом
shared-memory буферов, подключает IPC к event loop `appframework` и передаёт
нормализованные UI-события приложению.

## Имя библиотеки

`gui/ui`

## Контракты

1. Библиотека работает внутри процесса приложения и не является сервисом. Она
   не создаёт отдельный поток и не предоставляет собственный IPC endpoint.
2. Библиотека используется нативными GUI-приложениями и графической оболочкой.
   Widget toolkit строится поверх неё и не входит в базовый контракт `uilib`.
3. `uilib` использует `appframework`, `libcore`, `libmem`, `libstr`, `libmath`,
   `libeventloop` и `ipclib`. Она не использует внутренние заголовки сервисов.
4. Все операции возвращают `ErrorCode` из `libcore`. `CORE_OK` является
   единственным успешным результатом.
5. Значения параметров с префиксом `out_` определены только при возврате
   `CORE_OK`, кроме poll/read-status операций, где контракт явно задаёт статус.
6. `UiContextHandle` создаётся через `ui_initialize()` и закрывается ровно один
   раз через `ui_shutdown()`. В процессе v1 допускается один контекст на один
   `AppHandle`.
7. `UiWindowHandle` создаётся через `ui_window_create()` и уничтожается через
   `ui_window_destroy()`. Окно принадлежит создавшему `UiContextHandle`.
8. При создании окна библиотека получает `SurfaceId` и `SurfaceBindToken` от
   `compositorserv`, затем передаёт их `windowserv` для связи с новым
   `WindowId`. Токен хранится внутри `UiWindowHandle` до уничтожения
   поверхности, не предоставляется приложению и используется повторно только
   при восстановлении связи с `windowserv`. При частичной ошибке уже созданный
   server-side объект удаляется до возврата.
9. Все операции и callbacks выполняются только в потоке-владельце `AppHandle`.
   Одновременные обращения к одному context/window запрещены.
10. Библиотека регистрирует IPC FD в event loop через `appframework`. Она не
    вызывает блокирующее ожидание и не выполняет callbacks из обработчика
    сигнала.
11. `UiWindowEvent`, переданный callback, является невладеющим и действителен
    только до возврата callback. Строки и массивы события не сохраняются без
    явного копирования приложением.
12. Первый `Configure` определяет размер окна. Клиент не выполняет present до
    получения configure. Serial подтверждается первым commit подходящего
    размера.
13. `ui_window_acquire_buffer()` возвращает writable mapping только свободного
    буфера. После успешного `ui_window_present()` память нельзя изменять до
    события release, обработанного библиотекой.
14. Библиотека поддерживает не менее двух буферов окна. Если свободного буфера
    нет, acquire возвращает `CORE_WOULD_BLOCK`; event loop не блокируется.
15. `ui_window_damage()` накапливает pending damage до present. Если damage не
    указан после configure, библиотека отправляет full damage.
16. `CloseRequested` является запросом и не уничтожает окно автоматически.
    Приложение отвечает `Allow`, `Deny` или `Defer`; при `Allow` завершает
    сохранение и уничтожает окно.
17. При разрыве соединения объект переходит в `Suspended`, освобождает
    server-side идентификаторы и уведомляет приложение событием `ServiceLost`.
    После восстановления библиотека пересоздаёт объекты и требует полный redraw.
18. `uilib` не читает физические устройства, не выбирает фокус и z-order, не
    открывает display backend и не управляет процессами.

### Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `appframework` | Event loop, жизненный цикл и регистрация IPC FD |
| `libcore` | Базовые типы, `ErrorCode`, размеры и проверки |
| `libmem` | Context, окна, очереди событий и buffer pool |
| `libstr` | UTF-8 заголовки и копии строк конфигурации |
| `libmath` | `GuiPoint`, `GuiSize`, `GuiRect` и clipping damage |
| `libeventloop` | Типы масок событий через API appframework |
| `special/ipc` | Неблокирующие соединения с GUI-сервисами и передача FD |
| `windowserv` | Window lifecycle, configure, focus и input events |
| `compositorserv` | Surfaces, buffer import, commit и frame callbacks |

### Состояния контекста

| Состояние | Допустимые операции | Переходы |
| --- | --- | --- |
| `Ready` | создание окон, dispatch, shutdown | `Suspended`, `Stopping` |
| `Suspended` | dispatch, reconnect, destroy окон, shutdown | `Ready`, `Stopping` |
| `Stopping` | уничтожение окон и соединений | `Destroyed` |
| `Destroyed` | Нет | Нет |

### Состояния окна

| Состояние | Допустимые операции | Переходы |
| --- | --- | --- |
| `Created` | ожидание configure, set title, destroy | `Configured`, `Suspended`, `Destroyed` |
| `Configured` | show, acquire, damage, present, set state, destroy | `Visible`, `Suspended`, `Destroyed` |
| `Visible` | acquire, damage, present, hide, set state, destroy | `Configured`, `Suspended`, `Destroyed` |
| `Suspended` | destroy, ожидание восстановления | `Created`, `Destroyed` |
| `Destroyed` | Нет | Нет |

### Модель буферов

| Состояние | Допустимые операции | Переходы |
| --- | --- | --- |
| `Free` | acquire, destroy | `Acquired`, `Destroyed` |
| `Acquired` | damage, present | `Submitted`, `Free` при отмене |
| `Submitted` | ожидание release | `Free`, `Destroyed` при потере сервиса |
| `Destroyed` | Нет | Нет |

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| --- | --- | --- | --- |
| `UiContextHandle` | Непрозрачный указатель: `struct UiContextHandle *` | `NULL` или живой context | Контекст GUI одного приложения |
| `UiWindowHandle` | Непрозрачный указатель: `struct UiWindowHandle *` | `NULL` или окно текущего context | Клиентское окно и surface |
| `UiBufferHandle` | Непрозрачный указатель: `struct UiBufferHandle *` | Буфер текущего окна | Элемент buffer pool |
| `UiConfig` | `usize preferred_buffer_count` | От `2` до `UI_BUFFER_COUNT_MAX` | Настройки контекста |
| `UiWindowConfig` | title, preferred size, role, parent, flags, callback и user data | Валидные UTF-8 и ненулевой callback | Конфигурация нового окна |
| `UiWindowState` | Перечисление | `Normal`, `Minimized`, `Maximized`, `Fullscreen` | Запрошенное или подтверждённое состояние |
| `UiWindowRole` | Перечисление | `TopLevel`, `Dialog`, `Popup` | Политическая роль окна |
| `UiCloseResponse` | Перечисление | `Allow`, `Deny`, `Defer` | Ответ на `CloseRequested` с его serial |
| `UiWindowEventKind` | Перечисление | configure, close, focus, pointer, key, touch, frame, service | Вид события callback |
| `UiWindowEvent` | Tagged union | Соответствует `kind` | Невладеющее событие окна |
| `UiMappedBuffer` | `UiBufferHandle buffer`, `MutableBytes pixels`, `u32 stride`, `GuiSize size`, format | Writable только в состоянии `Acquired` | Отображение для software rendering |
| `UiEventHandler` | `void (*)(UiWindowHandle, const UiWindowEvent *, void *)` | Ненулевой callback | Обработчик событий окна |

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `UI_BUFFER_COUNT_DEFAULT` | `2` | Размер buffer pool окна по умолчанию |
| `UI_BUFFER_COUNT_MAX` | `4` | Максимальное число буферов одного окна в v1 |
| `UI_WINDOW_TITLE_MAX_BYTES` | `256` | Максимальная длина UTF-8 заголовка |
| `UI_DAMAGE_MAX_RECTS` | `64` | Максимум pending damage-прямоугольников |
| `UI_WINDOW_RESIZABLE` | `1` | Окно разрешает изменение размера |
| `UI_WINDOW_DECORATED` | `2` | Окно запрашивает стандартные декорации toolkit/shell |
| `UI_WINDOW_ACCEPTS_FOCUS` | `4` | Окно допускает keyboard focus |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| --- | --- | --- | --- |
| `ui_initialize(app, config, out_context)` | `AppHandle app`, `const UiConfig *config`, `UiContextHandle *out_context` | `ErrorCode` | Создаёт context, подключается к сервисам и регистрирует IPC FD |
| `ui_window_create(context, config, out_window)` | `UiContextHandle context`, `const UiWindowConfig *config`, `UiWindowHandle *out_window` | `ErrorCode` | Создаёт surface и window с rollback при частичной ошибке |
| `ui_window_show(window)` | `UiWindowHandle window` | `ErrorCode` | Просит windowserv отобразить настроенное окно |
| `ui_window_hide(window)` | `UiWindowHandle window` | `ErrorCode` | Скрывает окно без уничтожения buffer pool |
| `ui_window_set_title(window, title)` | `UiWindowHandle window`, `const String *title` | `ErrorCode` | Копирует и отправляет новый UTF-8 заголовок |
| `ui_window_request_state(window, state)` | `UiWindowHandle window`, `UiWindowState state` | `ErrorCode` | Запрашивает изменение состояния; результат приходит configure-событием |
| `ui_window_respond_close(window, serial, response)` | окно, serial `CloseRequested`, `UiCloseResponse response` | `ErrorCode` | Разрешает, отклоняет или откладывает закрытие |
| `ui_window_set_constraints(window, min_size, max_size)` | окно и допустимые размеры | `ErrorCode` | Устанавливает ограничения клиентской области |
| `ui_window_acquire_buffer(window, out_mapping)` | `UiWindowHandle window`, `UiMappedBuffer *out_mapping` | `ErrorCode` | Возвращает свободный writable buffer либо `CORE_WOULD_BLOCK` |
| `ui_window_damage(window, rects, count)` | окно, массив `GuiRect`, `usize count` | `ErrorCode` | Добавляет области к pending damage |
| `ui_window_present(window, buffer)` | `UiWindowHandle window`, `UiBufferHandle buffer` | `ErrorCode` | Attach, подтверждение configure и атомарный surface commit |
| `ui_window_destroy(window)` | `UiWindowHandle *window` | `ErrorCode` | Удаляет server-side объекты, buffer pool и записывает `NULL` |
| `ui_shutdown(context)` | `UiContextHandle *context` | `ErrorCode` | Уничтожает оставшиеся окна, соединения и источники event loop |

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Передан `NULL`, неверный callback, UTF-8, размер, role, state или rect |
| `CORE_INVALID_STATE` | Операция запрещена состоянием context, окна или buffer |
| `CORE_NOT_FOUND` | Endpoint сервиса отсутствует или server-side объект потерян |
| `CORE_PERMISSION_DENIED` | Сервис отклонил parent, input serial или доступ к объекту |
| `CORE_OUT_OF_MEMORY` | Нельзя создать context, окно, очередь, backing storage или buffer pool |
| `CORE_OVERFLOW` | Размер поверхности, stride или список damage выходит за пределы |
| `CORE_WOULD_BLOCK` | Нет свободного буфера или заполнена исходящая IPC-очередь |
| `CORE_IO_ERROR` | Ошибка shared memory, IPC или повреждённое сообщение сервиса |
| `CORE_UNSUPPORTED` | Версия протокола, формат, role или возможность не поддерживается |
