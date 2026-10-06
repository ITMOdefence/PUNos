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
| `IMPLEMENTED` | Код и compile-time проверки добавлены; сквозной runtime-тест выполняется в Linux x86_64/QEMU |

## Итог аудита

| Сценарий | Контракт GUI | Результат аудита |
| --- | --- | --- |
| Запуск сервисов | display → compositor → window | Протоколы согласованы; `initserv` ещё не содержит GUI-сервисы и readiness |
| Создание app view | surface/token → window → scene binding | Согласовано; нужен безопасный генератор токенов |
| Первый кадр | buffer import → commit → configure ack → show | Согласовано; нужны shareable memory и передача FD |
| Rotation/system bars | configure(viewport, scale, orientation, insets) → новый buffer commit → ack | Согласовано; старый кадр действует до ack |
| Composition/present | compositor output FD → display queue → completion | Согласовано; нужны FD transport и graphics backend ABI |
| Touch/pointer/key | IOserv frame → shell policy/window routing → client event | Touch является основным вводом; timestamp приведён к `MonotonicTime` |
| Hotplug | subscription snapshot → generation → redraw | Согласовано; snapshot защищён `snapshot_serial` |
| Перезапуск displayserv | новые DisplayId → compositor reconnect/full redraw | Согласовано; startup token должен переживать рестарты зависимостей |
| Перезапуск compositorserv | новые SurfaceId/token → rebind app view | Согласовано |
| Перезапуск windowserv | новые WindowId → повторный create через uilib | Согласовано |

Неразрешённых противоречий между пятью GUI-протоколами после аудита нет.
Рабочую реализацию блокируют системные зависимости ниже.

## Блокирующие зависимости

### B1. Стабильный wire ABI ErrorCode

| Поле | Значение |
| --- | --- |
| Статус | `IMPLEMENTED` |
| Владелец | Системная команда, `libcore` |
| Потребители | Все GUI-протоколы |

`ResultPrefix` кодирует ошибку как `u32`. `libcore` закрепляет явные числовые
значения `CORE_OK = 0` и остальных `CORE_*`; существующие значения запрещено
перенумеровывать. Wire-кодек записывает значение как `u32`, не копируя память
нативного enum.

1. назначить каждому публичному `CORE_*` постоянное значение `u32`;
2. закрепить `CORE_OK = 0`;
3. запретить перенумерацию опубликованных значений;
4. определить обработку неизвестного значения при декодировании.

Неизвестное значение при декодировании считается protocol error.

### B2. Передача FD через ipclib

| Поле | Значение |
| --- | --- |
| Статус | `IMPLEMENTED` |
| Владелец | Системная команда, Syscall Wrappers и `ipclib` |
| Потребители | `buffer.import`, `display.present` |

Реализованы:

1. безопасные wrappers `sendmsg()` и `recvmsg()`;
2. типы для `msghdr`, `cmsghdr`, `iovec` и `SCM_RIGHTS` без подключения
   внутренних libc-заголовков;
3. API `ipclib`, передающее точное ограниченное число FD вместе с одним кадром;
4. `CLOEXEC` для принятых FD;
5. закрытие всех лишних и непотреблённых FD при malformed frame, overflow,
   disconnect и ошибке импорта;
6. compile-time ABI test и сквозной тест передачи shared-memory FD; стресс-тест
   partial send/receive и подсчёта утечек выполняется в QEMU-профиле.

Для GUI v1 достаточно максимум одного FD на сообщение. Копирование полного
кадра в IPC payload не является публичной альтернативой.

### B3. Разделяемый backing storage

| Поле | Значение |
| --- | --- |
| Статус | `IMPLEMENTED` |
| Владелец | Системная команда, Native Runtime/System Libs |
| Потребители | `uilib`, `compositorserv`, `displayserv` |

`libmem` предоставляет публичный объект разделяемой памяти с операциями:

```text
create(size) → owned fd + writable mapping
get_size(fd)
map_readonly(fd, size)
unmap(mapping)
close(fd)
```

Реализация использует `memfd_create()` + `ftruncate()` + `mmap()`. Объект
создаётся с `CLOEXEC`, размер после импорта проверяется через
`fstat()`, executable mapping запрещён. Seal от изменения размера желателен,
если соответствующий ABI будет доступен.

### B4. Криптографически случайные токены

| Поле | Значение |
| --- | --- |
| Статус | `IMPLEMENTED` |
| Владелец | Системная команда, Native Runtime/System Libs |
| Потребители | `PresenterToken`, `ControllerToken`, `ShellToken`, `SurfaceBindToken` |

Syscall Wrappers предоставляет `getrandom()`, а `librandom` полностью заполняет
переданный буфер либо возвращает ошибку. Слабый PRNG, время, PID и
последовательный счётчик для capability-токенов запрещены.

`SurfaceBindToken` генерирует compositorserv. Startup tokens генерирует
владелец запуска и хранит только в памяти.

### B5. Реестр, readiness и перезапуск GUI-сервисов

| Поле | Значение |
| --- | --- |
| Статус | `PARTIAL` — blocker для полного GUI stack |
| Владелец | Системная команда, `initserv`/`appserv` |
| Потребители | Весь GUI stack |

Текущий `initserv` уже генерирует `PresenterToken`, запускает и перезапускает
`displayserv`. `compositorserv`, `windowserv` и общий readiness-протокол пока
отсутствуют. Нужен полный порядок:

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
| Статус | `IMPLEMENTED` |
| Владелец | Системная команда совместно с GUI-командой |
| Потребители | `displaylib`, `displayserv` |

`libgraphics` предоставляет публичный System graphics ABI и первый программный
framebuffer backend. ABI предоставляет:

- обнаружение/open графического устройства;
- перечисление outputs и modes;
- configure output;
- импорт/отображение кадрового FD;
- неблокирующие hotplug и present-completion events;
- безопасное восстановление после device loss.

Для первой реализации допустим software/fb backend, если он соблюдает тот же
контракт displaylib. Детали DRM/KMS не должны попадать в протокол приложений.

### B7. Графический профиль QEMU

| Поле | Значение |
| --- | --- |
| Статус | `BLOCKER` для сквозного GUI-теста |
| Владелец | Системная команда совместно с GUI-командой |
| Потребители | displaylib, IOserv и интеграционные тесты GUI |

Текущий `run-qemu-initramfs.sh` использует `-nographic` и проверяет только
serial boot. Нужен отдельный сценарий, который не ломает headless CI-профиль и
запускает мобильную конфигурацию эмулятора:

- `virtio-gpu`/DRM-KMS display с фиксированным portrait mode для первого MVP;
- `virtio-input` pointer как одно касание и воспроизводимый touch test source;
- отдельная serial console для журнала;
- заданные logical size, scale и orientation;
- возможность сделать screenshot/hash тестового кадра в CI;
- отсутствие QEMU-specific типов в публичных GUI-протоколах.

Поддержка реального ARM64-устройства не входит в v1 до выбора конкретной
платы, kernel config, device tree и display/touch backend.

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

Начальная реализация находится в `GUI/Protocols`. Она уже кодирует и
декодирует FD-bearing путь `buffer.import`, `surface.damage` и
`display.present`. Расширение остальными сообщениями выполняется по тем же
правилам. Модуль:

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
| Атомарная транзакция нескольких SceneNode | `LATER` | Новое view настраивается скрытым |
| Hardware input timestamp | `LATER` | Время обработки InputFrame в windowserv |
| Color management/HDR | `LATER` | sRGB-подобный XRGB/ARGB8888 |
| Fractional scale | `LATER` | Scale `1.0`, один surface pixel на logical pixel |
| Экранная клавиатура и IME | `LATER` | В v1 только аппаратные key events; text field не входит в MVP |
| GPU composition/animations | `LATER` | Software composition и дискретная смена foreground view |

## Порядок реализации

1. Закрепить `ErrorCode` wire ABI.
2. Добавить secure random и shareable memory в системный слой.
3. Расширить wrappers/ipclib передачей одного FD.
4. Добавить графический QEMU-профиль и тестовый display/input backend.
5. Добавить GUI-сервисы, readiness и token handoff в initserv/appserv.
6. Реализовать общий GUI wire codec и protocol-vector tests.
7. Реализовать displaylib и displayserv.
8. Реализовать compositorserv: surface, renderer, scene и display client.
9. Реализовать windowserv: foreground lifecycle, mobile scene policy,
   configure с orientation/insets и touch routing.
10. Реализовать system shell: Home, app switch, bars, lock и Back policy.
11. Реализовать uilib: reconnect, app-view lifecycle и buffer pool.
12. Реализовать mobile toolkit поверх стабильной uilib.

Пункты 1–4 можно выполнять параллельно разными владельцами после согласования
ABI. Реализация сервисов начинается с mock backend только при сохранении тех же
wire contracts и правил владения.

## Критерий готовности инфраструктуры

Системный фундамент готов для GUI v1, когда интеграционный тест может:

1. запустить три GUI-сервиса в правильном порядке;
2. назначить presenter и scene-controller после рестарта каждого процесса;
3. создать shareable buffer, передать его FD и проверить отсутствие утечки;
4. показать кадр и получить финальный present completion;
5. создать app view, пройти configure/commit/ack, активировать его и получить
   touch event;
6. отключить display/compositor/windowserv по очереди и восстановить стек без
   использования старых идентификаторов.
