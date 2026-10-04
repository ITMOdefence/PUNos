# Зависимости реализации GUI-протоколов

## Назначение

Документ связывает базовый, surface, scene, display и window протоколы с
возможностями системного слоя. Он фиксирует порядок реализации и отделяет
готовые GUI-контракты от отсутствующей инфраструктуры.

Статусы:

| Статус | Значение |
| --- | --- |
| `BLOCKER` | Без зависимости нельзя реализовать публичный протокол v1 |
| `REQUIRED` | Нужна для полного рабочего стека, но не блокирует разработку независимых частей |
| `LATER` | Допустимое ограничение v1 с определённой заменой |

## Итог аудита

| Сценарий | Контракт GUI | Результат аудита |
| --- | --- | --- |
| Запуск сервисов | display → compositor → window | Протоколы согласованы; `initserv` ещё не содержит GUI-сервисы и readiness |
| Создание окна | surface/token → window → scene binding | Согласовано; нужен безопасный генератор токенов |
| Первый кадр | buffer import → commit → configure ack → show | Согласовано; нужны shareable memory и передача FD |
| Resize | configure → новый buffer commit → ack → scene geometry | Согласовано; старый кадр действует до ack |
| Composition/present | compositor output FD → display queue → completion | Согласовано; нужны FD transport и graphics backend ABI |
| Pointer/key/touch | IOserv frame → window routing → client event | Согласовано; timestamp приведён к `MonotonicTime` в миллисекундах |
| Hotplug | subscription snapshot → generation → redraw | Согласовано; snapshot защищён `snapshot_serial` |
| Перезапуск displayserv | новые DisplayId → compositor reconnect/full redraw | Согласовано; startup token должен переживать рестарты зависимостей |
| Перезапуск compositorserv | новые SurfaceId/token → rebind окна | Согласовано |
| Перезапуск windowserv | новые WindowId → повторный create через uilib | Согласовано |

Неразрешённых противоречий между пятью GUI-протоколами после аудита нет.
Рабочую реализацию блокируют системные зависимости ниже.

## Блокирующие зависимости

### B1. Стабильный wire ABI ErrorCode

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` |
| Владелец | Системная команда, `libcore` |
| Потребители | Все GUI-протоколы |

`ResultPrefix` кодирует ошибку как `u32`, но `libcore` пока перечисляет только
имена без закреплённых числовых значений. Системный ABI должен:

1. назначить каждому публичному `CORE_*` постоянное значение `u32`;
2. закрепить `CORE_OK = 0`;
3. запретить перенумерацию опубликованных значений;
4. определить обработку неизвестного значения при декодировании.

До этого сериализация нативного enum через `memcpy` запрещена.

### B2. Передача FD через ipclib

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` |
| Владелец | Системная команда, Syscall Wrappers и `ipclib` |
| Потребители | `buffer.import`, `display.present` |

Необходимы:

1. безопасные wrappers `sendmsg()` и `recvmsg()`;
2. типы для `msghdr`, `cmsghdr`, `iovec` и `SCM_RIGHTS` без подключения
   внутренних libc-заголовков;
3. API `ipclib`, передающее точное ограниченное число FD вместе с одним кадром;
4. `CLOEXEC` для принятых FD;
5. закрытие всех лишних и непотреблённых FD при malformed frame, overflow,
   disconnect и ошибке импорта;
6. тесты partial send/receive и утечки FD.

Для GUI v1 достаточно максимум одного FD на сообщение. Копирование полного
кадра в IPC payload не является публичной альтернативой.

### B3. Разделяемый backing storage

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` |
| Владелец | Системная команда, Native Runtime/System Libs |
| Потребители | `uilib`, `compositorserv`, `displayserv` |

Текущий `libmem` создаёт анонимный `mmap`, который нельзя передать другому
процессу как FD. Требуется публичный объект разделяемой памяти с операциями:

```text
create(size) → owned fd + writable mapping
get_size(fd)
map_readonly(fd, size)
unmap(mapping)
close(fd)
```

Предпочтительная реализация Linux — `memfd_create()` + `ftruncate()` +
`mmap()`. Объект создаётся с `CLOEXEC`, размер после импорта проверяется через
`fstat()`, executable mapping запрещён. Seal от изменения размера желателен,
если соответствующий ABI будет доступен.

### B4. Криптографически случайные токены

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` |
| Владелец | Системная команда, Native Runtime/System Libs |
| Потребители | `PresenterToken`, `ControllerToken`, `ShellToken`, `SurfaceBindToken` |

В репозитории нет публичного источника системной случайности. Требуется wrapper
`getrandom()` и библиотечная операция, которая полностью заполняет заданный
буфер либо возвращает ошибку. Слабый PRNG, время, PID и последовательный
счётчик для capability-токенов запрещены.

`SurfaceBindToken` генерирует compositorserv. Startup tokens генерирует
владелец запуска и хранит только в памяти.

### B5. Реестр, readiness и перезапуск GUI-сервисов

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` |
| Владелец | Системная команда, `initserv`/`appserv` |
| Потребители | Весь GUI stack |

Текущий реестр `initserv` не содержит `displayserv`, `compositorserv` и
`windowserv`, а общий readiness-протокол отсутствует. Нужен порядок:

```text
logserv
├── IOserv
└── displayserv ready
      └── compositorserv bind presenter + ready
            └── windowserv bind controller + subscribe IOserv + ready
                  └── GUI-приложения и shell
```

Одного успешного `process_start` недостаточно. Зависимый сервис запускается
после готовности endpoint и успешной обязательной операции bind/subscribe.

Startup tokens живут до завершения загрузочной сессии, а не до завершения
одного процесса. При независимом перезапуске участники пары получают тот же
токен:

| Токен | Получатели |
| --- | --- |
| `PresenterToken` | displayserv, compositorserv |
| `ControllerToken` | compositorserv, windowserv |
| `ShellToken` | windowserv, appserv → системная оболочка |

Передача `ShellToken` требует защищённого handoff через `appserv`, потому что
shell является пользовательским приложением и не запускается напрямую
`initserv`.

### B6. Системный graphics backend ABI

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` |
| Владелец | Системная команда совместно с GUI-командой |
| Потребители | `displaylib`, `displayserv` |

`displaylib` ссылается на публичный System graphics ABI, которого пока нет в
документации. Минимальный ABI должен предоставлять:

- обнаружение/open графического устройства;
- перечисление outputs и modes;
- configure output;
- импорт/отображение кадрового FD;
- неблокирующие hotplug и present-completion events;
- безопасное восстановление после device loss.

Для первой реализации допустим software/fb backend, если он соблюдает тот же
контракт displaylib. Детали DRM/KMS не должны попадать в протокол приложений.

## Обязательные интеграции

### R1. Монотонное время

| Поле | Значение |
| --- | --- |
| Статус | `REQUIRED` |
| Владелец | `libtime`, GUI-сервисы |

Все публичные GUI timestamp используют `MonotonicTime` (`u64` миллисекунд),
как определено `libtime`. Displayserv ставит время завершения present,
windowserv — время обработки входного кадра. Wall clock в протоколах GUI не
используется.

### R2. Авторизация IOserv

| Поле | Значение |
| --- | --- |
| Статус | `REQUIRED` |
| Владелец | Системная команда |

IOserv пока считает endpoint внутренним, но не отличает `windowserv` от
произвольного локального клиента. До peer credentials доступ ограничивается
правами endpoint. Для production boundary нужен startup token или проверка
`SO_PEERCRED` по той же модели, что у GUI-сервисов.

### R3. Общий wire codec GUI

| Поле | Значение |
| --- | --- |
| Статус | `REQUIRED` |
| Владелец | GUI-команда |

Нужен внутренний модуль кодирования/декодирования, который:

- читает little-endian поля по смещениям без cast к C-структурам;
- проверяет длины и арифметику до выделения памяти;
- валидирует bool, enum, UTF-8 и trailing bytes;
- создаёт `RequestPrefix`/`ResultPrefix`;
- централизованно закрывает непотреблённые FD;
- тестируется одинаковыми wire vectors для клиента и сервиса.

Этот модуль является реализационной деталью GUI и не меняет `ipclib`.

## Отложенные улучшения

| Зависимость | Статус | Замена в v1 |
| --- | --- | --- |
| Peer credentials (`SO_PEERCRED`) | `LATER` | Startup tokens и права Unix endpoint |
| Explicit GPU fences | `LATER` | Completion events и software composition |
| Атомарная транзакция нескольких SceneNode | `LATER` | Новое окно настраивается скрытым |
| Hardware input timestamp | `LATER` | Время обработки InputFrame в windowserv |
| Color management/HDR | `LATER` | sRGB-подобный XRGB/ARGB8888 |
| Fractional scale | `LATER` | Scale `1.0`, один surface pixel на logical pixel |

## Порядок реализации

1. Закрепить `ErrorCode` wire ABI.
2. Добавить secure random и shareable memory в системный слой.
3. Расширить wrappers/ipclib передачей одного FD.
4. Добавить GUI-сервисы, readiness и token handoff в initserv/appserv.
5. Реализовать общий GUI wire codec и protocol-vector tests.
6. Реализовать displaylib и displayserv.
7. Реализовать compositorserv: surface, renderer, scene и display client.
8. Реализовать windowserv: scene controller, configure и input routing.
9. Реализовать uilib: reconnect, window lifecycle и buffer pool.
10. Реализовать toolkit поверх стабильной uilib.

Пункты 1–4 можно выполнять параллельно разными владельцами после согласования
ABI. Реализация сервисов начинается с mock backend только при сохранении тех же
wire contracts и правил владения.

## Критерий готовности инфраструктуры

Системный фундамент готов для GUI v1, когда интеграционный тест может:

1. запустить три GUI-сервиса в правильном порядке;
2. назначить presenter и scene-controller после рестарта каждого процесса;
3. создать shareable buffer, передать его FD и проверить отсутствие утечки;
4. показать кадр и получить финальный present completion;
5. создать окно, пройти configure/commit/ack и получить input event;
6. отключить display/compositor/windowserv по очереди и восстановить стек без
   использования старых идентификаторов.
