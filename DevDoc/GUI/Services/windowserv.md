## Описание

windowserv — системный сервис графической подсистемы PUNos, который управляет
окнами, их состояниями и размещением, keyboard/pointer focus, модальными
связями и маршрутизацией физических событий ввода в клиентские окна.

Пиксельное содержимое окон принадлежит compositorserv. windowserv хранит
только policy и передаёт compositorserv геометрию, видимость и порядок surface.

## Имя сервиса

| Поле | Значение |
| --- | --- |
| Идентификатор | windowserv |
| Исполняемый файл | windowserv |
| Владелец запуска | initserv |
| IPC endpoint | /run/punos/windowserv.sock |
| Версия протокола | v1 |

## Ответственность и границы

- windowserv владеет WindowId, деревом parent/modal связей и состоянием окон.
- windowserv определяет geometry, visibility, z-order и активное окно.
- windowserv подписывается на IOserv и маршрутизирует keyboard, pointer и touch.
- windowserv преобразует глобальные координаты в локальные координаты окна.
- windowserv подтверждает изменения geometry через configure serial.
- windowserv управляет сценой через привилегированный канал compositorserv.
- windowserv не читает evdev, не преобразует клавиши в Unicode, не рисует
  decorations/widgets и не отображает shared-memory buffers.
- windowserv не завершает приложения: принудительный lifecycle принадлежит appserv.

## Зависимости

| Зависимость | Использование |
| --- | --- |
| sys/core | ErrorCode, GUI id, базовые типы и проверка IPC payload |
| sys/memory | Окна, client state, focus stack, grabs и очереди configure |
| sys/string | Заголовки окон и IPC endpoint |
| sys/math | Geometry, hit testing, display bounds и ограничения размера |
| sys/event | Event loop endpoint, клиентов, IOserv и compositorserv |
| special/ipc | IPC-соединения, сообщения и исходящие очереди |
| IOserv | Device events и InputFrame keyboard/pointer/touch |
| compositorserv | Scene binding, geometry, visibility, stack и outputs |
| logserv | Lifecycle, policy changes, protocol errors и recovery |

## Жизненный цикл

### Запуск

| Параметр | Значение |
| --- | --- |
| Готовые сервисы до запуска | logserv, IOserv, compositorserv |
| Параметры | Controller secret compositorserv и политика начального workspace |
| Успех | Создан event loop; выполнены IOserv subscribe и controller bind; создан endpoint |
| IOserv временно недоступен | Ошибка запуска v1; initserv повторяет запуск |
| Ошибка compositor controller bind | Завершение с PROCESS_EXIT_INITIALIZATION_FAILED |

### Остановка и восстановление

При остановке windowserv прекращает accept, отменяет grabs/touch sequences,
unmap всех окон через compositorserv, закрывает клиентов и сервисные
соединения, затем endpoint и event loop.

При разрыве IOserv сервис сбрасывает pressed state, pointer capture и touch
ownership, остаётся Ready без ввода и пытается переподключиться. При разрыве
compositorserv окна сохраняются логически, переходят в Unmapped и клиентам
отправляется window.rendering_suspended. После reconnect поверхности должны
быть повторно привязаны. При собственном перезапуске все WindowId теряются.

### Состояния сервиса

| Состояние | Допустимые действия | Переходы |
| --- | --- | --- |
| Starting | Подключение зависимостей, event loop и endpoint | Ready, Failed |
| Ready | Window IPC, policy, input routing и scene updates | Degraded, Stopping, Failed |
| Degraded | Window state и reconnect; часть input/rendering недоступна | Ready, Stopping, Failed |
| Stopping | Unmap, отмена input и освобождение ресурсов | Stopped |
| Failed | Новые операции не принимаются | Завершение процесса |

### Состояния окна

| Состояние | Значение |
| --- | --- |
| Created | Объект создан, первый configure ещё не подтверждён |
| Hidden | Окно настроено, но не отображается |
| Visible | Окно отображается в Normal state |
| Minimized | Объект сохранён, surface unmapped |
| Maximized | Окно занимает рабочую область display |
| Fullscreen | Окно занимает выбранный display |
| Closing | Отправлен close_requested; новые state requests отклоняются |

## Владение ресурсами

| Ресурс | Владелец | Создание | Освобождение |
| --- | --- | --- | --- |
| WindowId и WindowState | windowserv для клиентского соединения | window.create | window.destroy или disconnect |
| Заголовок окна | windowserv | create/set_title | Следующий set_title или destroy |
| Focus, z-order и modal tree | windowserv | При изменении policy | При destroy/disconnect/остановке |
| Pointer grab/touch ownership | windowserv | По входному serial | При завершении sequence, cancel или disconnect |
| Surface binding | compositorserv по команде windowserv | window.create/rebind | window.destroy или render suspend |
| IOserv/compositorserv connections | windowserv | При запуске/reconnect | При разрыве или остановке |
| IPC server/client connections | windowserv через ipclib | При запуске/accept | При disconnect или остановке |

SurfaceId остаётся объектом compositorserv и не переходит во владение windowserv.

## IPC-контракт

### Клиенты и доступ

| Клиент | Разрешённые операции | Проверка доступа |
| --- | --- | --- |
| uilib приложения | window.* только для собственных окон | Владение IPC-соединения |
| Desktop shell | Обычные window.* и системные shell policy extensions | Доверенный shell-клиент v1 |

Peer identity отсутствует. Обычное владение проверяется по соединению; роль
shell в v1 назначается одноразовым startup secret, переданным initserv.

### Формат payload

Все целые поля little-endian, строки UTF-8 без NUL. Каждый запрос содержит
protocol_version и request_id. Указатели, FD и compositor handles не передаются.

| message_type | Направление | Payload |
| --- | --- | --- |
| 1, window.create | Клиент → сервис | WindowCreateRequest |
| 2, window.create_result | Сервис → клиент | WindowCreateResult |
| 3, window.destroy | Клиент → сервис | WindowIdRequest |
| 4, window.destroy_result | Сервис → клиент | BasicResult |
| 5, window.show | Клиент → сервис | WindowIdRequest |
| 6, window.hide | Клиент → сервис | WindowIdRequest |
| 7, window.set_title | Клиент → сервис | WindowSetTitleRequest |
| 8, window.request_state | Клиент → сервис | WindowStateRequest |
| 9, window.set_constraints | Клиент → сервис | WindowConstraintsRequest |
| 10, window.ack_configure | Клиент → сервис | WindowAckConfigureRequest |
| 11, window.begin_move | Клиент → сервис | WindowInteractiveRequest |
| 12, window.begin_resize | Клиент → сервис | WindowResizeRequest |
| 13, window.set_modal | Клиент → сервис | WindowModalRequest |
| 14, window.operation_result | Сервис → клиент | BasicResult |
| 15, window.configure | Сервис → клиент | WindowConfigureEvent |
| 16, window.focus_changed | Сервис → клиент | WindowFocusEvent |
| 17, window.close_requested | Сервис → клиент | WindowCloseEvent |
| 18, window.visibility_changed | Сервис → клиент | WindowVisibilityEvent |
| 19, window.pointer | Сервис → клиент | WindowPointerEvent |
| 20, window.key | Сервис → клиент | WindowKeyEvent |
| 21, window.touch | Сервис → клиент | WindowTouchEvent |
| 22, window.rendering_suspended | Сервис → клиент | WindowServiceEvent |
| 23, window.surface_rebind | Клиент → сервис | WindowSurfaceRebindRequest |

### Операции

| Операция | Запрос | Успешный ответ | Назначение |
| --- | --- | --- | --- |
| window.create | WindowCreateRequest | WindowCreateResult | Создаёт окно для surface клиента |
| window.destroy | WindowIdRequest | BasicResult | Удаляет окно и scene binding |
| window.show/hide | WindowIdRequest | BasicResult | Map/unmap без уничтожения |
| window.set_title | WindowSetTitleRequest | BasicResult | Копирует заголовок окна |
| window.request_state | WindowStateRequest | BasicResult + configure event | Запрашивает minimize/maximize/restore/fullscreen |
| window.set_constraints | WindowConstraintsRequest | BasicResult | Задаёт min/max клиентской области |
| window.ack_configure | WindowAckConfigureRequest | BasicResult | Подтверждает configure serial |
| window.begin_move/resize | Interactive request | BasicResult | Начинает операцию по свежему input serial |
| window.set_modal | WindowModalRequest | BasicResult | Устанавливает/снимает модального родителя |
| window.surface_rebind | WindowSurfaceRebindRequest | BasicResult | Повторно связывает surface после compositor restart |

### События и подписки

| Событие | Получатель | Когда отправляется |
| --- | --- | --- |
| window.configure | Владелец окна | Новый размер, state, scale или display |
| window.focus_changed | Старый и новый focus owners | Изменился keyboard focus |
| window.close_requested | Владелец окна | Пользователь/shell запросил закрытие |
| window.visibility_changed | Владелец окна | Map, unmap, minimize или occlusion state |
| window.pointer/key/touch | Выбранный input owner | IOserv frame маршрутизирован окну |
| window.rendering_suspended | Все клиенты с окнами | Потеряна связь с compositorserv |

### Типы сообщений

| Тип | Поля | Ограничения | Назначение |
| --- | --- | --- | --- |
| WindowCreateRequest | version, request_id, surface_id, role, parent_id, preferred size, flags, title length и bytes | UTF-8 до 256 байт; parent своего клиента | Создание окна |
| WindowCreateResult | ErrorCode error, request_id, window_id | id определён при CORE_OK | Результат создания |
| WindowIdRequest | version, request_id, window_id | Окно своего соединения | Выбор окна |
| WindowSetTitleRequest | version, request_id, window_id, u16 length, bytes | UTF-8 до 256 байт | Новый заголовок |
| WindowStateRequest | version, request_id, window_id, state, display_id | display нужен для fullscreen | Запрос состояния |
| WindowConstraintsRequest | version, request_id, window_id, min_size, max_size | min <= max; ноль означает отсутствие границы | Размерные ограничения |
| WindowAckConfigureRequest | version, request_id, window_id, configure_serial | Только последний неустаревший serial | Подтверждение configure |
| WindowInteractiveRequest | version, request_id, window_id, input_serial | Свежий pointer/button serial этого окна | Move |
| WindowResizeRequest | те же поля и u8 edge_mask | Непустая допустимая edge mask | Resize |
| WindowConfigureEvent | window_id, serial, geometry, state, scale, display_id | serial монотонен для окна | Новая конфигурация |
| WindowPointerEvent | window_id, input_serial, timestamp, kind, local point, button/axis data | Координаты внутри/около клиентской области | Pointer input |
| WindowKeyEvent | window_id, input_serial, timestamp, code, state, modifiers | Linux key code без Unicode | Keyboard input |
| WindowTouchEvent | window_id, input_serial, timestamp, touch_id, phase, local point | touch_id закреплён до up/cancel | Touch input |

## Контракты

1. CORE_OK является единственным успешным кодом ответа.
2. WindowId уникален до destroy или перезапуска windowserv и принадлежит
   создавшему соединению.
3. SurfaceId проверяется через compositorserv и связывается не более чем с
   одним живым окном.
4. Первый configure обязателен до show и present содержимого.
5. Новый configure заменяет неподтверждённый старый; ack устаревшего serial
   возвращает CORE_INVALID_STATE.
6. Клиент не задаёт абсолютную geometry или z-index обычного top-level окна.
7. Keyboard event получает focus owner; pointer event — hit-test owner или grab owner.
8. Touch sequence закрепляется за окном первого touch down до up/cancel.
9. Модальное окно блокирует ввод в цепочку родителей ниже него.
10. Hidden, Minimized и Closing окна не получают новый пользовательский ввод.
11. begin_move/resize разрешены только по свежему input serial этого клиента.
12. Disconnect удаляет окна, отменяет grabs и пересчитывает focus без утечки scene bindings.

## Потоковая модель и асинхронность

windowserv v1 однопоточный. Один event-loop поток владеет окнами, focus,
scene policy, IPC и состоянием ввода. IOserv InputFrame обрабатывается целиком
до следующего IPC-события, сохраняя порядок событий. Worker threads и
блокирующие операции отсутствуют.

## Конфигурация и постоянные данные

| Данные | Источник | Владелец | Действие при ошибке |
| --- | --- | --- | --- |
| Controller/shell secrets | Одноразовые аргументы initserv | windowserv | Привилегированная роль не назначается |
| Начальная рабочая область | Встроенная политика платформы | windowserv | Используется безопасный layout одного display |
| Окна, focus, grabs и placement | Только память процесса | windowserv | Теряются при перезапуске; клиенты пересоздают |

Сохранение положения окон, virtual desktops и user preferences отсутствуют.

## Логирование и наблюдаемость

В logserv записываются запуск/остановка; connect/disconnect; create/destroy и
state changes без заголовка окна; focus changes; rejected serial; invalid
payload; потеря IOserv/compositorserv; переполнение очереди клиента. Коды
нажатых клавиш, текст, pointer coordinates и touch positions не журналируются.

## Ошибки

| ErrorCode | Когда возвращается клиенту |
| --- | --- |
| CORE_INVALID_ARGUMENT | Неверный payload, UTF-8, role, state, size, edge или flags |
| CORE_INVALID_STATE | Устаревший configure, недопустимый переход или rendering suspended |
| CORE_PERMISSION_DENIED | Чужое окно/surface, privileged operation или старый input serial |
| CORE_NOT_FOUND | WindowId, parent, SurfaceId или DisplayId отсутствует |
| CORE_OUT_OF_MEMORY | Нельзя создать окно, строку, policy state или ответ |
| CORE_OVERFLOW | Размер payload, дерева или очереди превышает лимит |
| CORE_WOULD_BLOCK | Исходящая очередь клиента временно заполнена |
| CORE_IO_ERROR | Ошибка IPC с IOserv/compositorserv или повреждённый кадр |
| CORE_UNSUPPORTED | Версия, role, state или extension не поддерживается |

## Ограничения версии

- Один workspace; нет virtual desktops и сохранения placement.
- Нет clipboard, drag-and-drop, IME, Unicode layout и global shortcuts.
- Нет server-side decorations и accessibility tree.
- Нет надёжной peer identity; privileged roles используют startup secrets.
- Принудительное закрытие процесса и timeout policy принадлежат appserv/shell.
