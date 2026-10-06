## Описание

`windowserv` — сервис мобильной scene policy PUNos. Он связывает поверхности с
представлениями приложений, выбирает одно foreground app view, управляет Home,
dialog, системными слоями и lock screen, формирует configure с orientation и
safe insets и маршрутизирует touch-first ввод.

## Имя сервиса

| Поле | Значение |
| --- | --- |
| Идентификатор | windowserv |
| Исполняемый файл | windowserv |
| Владелец запуска | initserv |
| IPC endpoint | `/run/punos/windowserv.sock` |
| Версия протокола | v1 mobile |

## Ответственность и границы

- Владеет `WindowId`, role, parent/modal tree и lifecycle.
- Поддерживает одно foreground `APP` или `HOME` на дисплее.
- Назначает geometry, scene layer, visibility и stack через scene-controller.
- Вычисляет viewport и safe insets из дисплея и системных панелей.
- Получает frames от `IOserv`, применяет shell gesture policy и доставляет
  touch, pointer и key events выбранному view.
- Отменяет активные touch sequences до смены владельца или lifecycle.
- Не читает пиксели, не рисует UI, не распознаёт widget gestures и не управляет
  процессами приложений.

## Зависимости

| Зависимость | Использование |
| --- | --- |
| sys/core | ErrorCode, идентификаторы и разбор payload |
| sys/memory | Таблицы view, scene policy, очереди и touch ownership |
| sys/string | UTF-8 title и endpoint |
| sys/eventloop | IPC, IOserv и reconnect без блокировки |
| sys/time | MonotonicTime событий |
| special/ipc | Соединения и ограниченные исходящие очереди |
| IOserv | InputFrame keyboard/pointer/touch |
| compositorserv | Scene binding, geometry, layer и visibility |
| displayserv | Display geometry, orientation и scale |
| logserv | Lifecycle и ошибки без пользовательского ввода |

## Запуск и восстановление

Порядок: `displayserv ready` → `compositorserv ready/controller bind` →
подписка на display и IOserv → endpoint ready → запуск shell. До готовности
compositor и display endpoint приложений не публикуется.

При потере IOserv сервис отправляет CANCEL для touch и очищает input state.
При потере compositor все view становятся `SUSPENDED`; после rebind приложения
пересоздают поверхности и получают полный configure. При потере display
foreground сохраняется логически, rendering приостанавливается.

## Состояния

| Состояние сервиса | Работа |
| --- | --- |
| `Starting` | Проверка токенов и подключение зависимостей |
| `Ready` | Lifecycle, scene policy, configure и input routing |
| `Degraded` | Зависимость потеряна; новые активации не выполняются |
| `Stopping` | CANCEL, скрытие scene и освобождение ресурсов |
| `Failed` | Endpoint не принимает операции |

View использует `HIDDEN`, `BACKGROUND`, `FOREGROUND`, `SUSPENDED`, `CLOSING`.
`window.show` переводит HIDDEN в BACKGROUND; только shell activation даёт
FOREGROUND. Home или другое app возвращает прежнее view в BACKGROUND.

## Мобильная scene policy

Слои снизу вверх: `BACKGROUND`, `APP`, `DIALOG`, `SYSTEM`, `LOCK`.

- `HOME` и пользовательский `APP` взаимно исключаются.
- Dialog видим только вместе со своим foreground parent.
- SYSTEM_BAR и SYSTEM_OVERLAY создаёт shell; их insets входят в configure.
- LOCK всегда выше остальных слоёв и забирает обычный ввод.
- IME зарезервирован в SYSTEM, но text protocol отсутствует в v1.
- Обычный клиент не вызывает scene API и не выбирает global geometry.

## Configure

Configure содержит logical viewport, integer scale, orientation и safe insets.
Он создаётся при первом show, смене display mode/orientation, изменении панелей
и восстановлении. До commit/ack нового размера остаётся предыдущий кадр или
системный фон. Более новый configure заменяет неподтверждённый старый.

## Input policy

Touch является основным вводом. Получатель первого DOWN владеет sequence до
UP/CANCEL. System gesture region получает sequence только по shell policy; она
не клонируется приложению. Dialog блокирует parent. Background, hidden,
suspended и closing view ввода не получают.

Pointer поддерживается для QEMU и планшетных устройств. Keyboard получает
foreground view с flag. Home, Power и Volume остаются у shell. Windowserv
нормализует orientation и координаты, а widget hit-test, tap, long press, pan
и scroll выполняет toolkit.

## IPC-контракт

Wire format определён в [window-protocol.md](window-protocol.md).

| Клиент | Доступ |
| --- | --- |
| uilib приложения | Собственные APP/DIALOG/TRANSIENT и lifecycle events |
| system shell | Привилегированные роли, activate, insets и navigation после bind |
| appframework | Прямого доступа нет; интеграция через uilib |

Один event-loop поток владеет IPC, policy, scene state и input ownership.
Медленный клиент отключается и не блокирует IOserv или composition.

## Владение и cleanup

`WindowId`, title, parent links и pending configure принадлежат соединению.
SceneNode принадлежит compositor, touch ownership — windowserv до завершения
sequence. Disconnect приложения удаляет его bindings и links. Disconnect
shell блокирует privileged операции, отменяет system gestures и активирует
lock surface, если она доступна, иначе системный фон.

## Логирование

Разрешены lifecycle transition, role, WindowId, размеры configure, причина
reconnect и ErrorCode. Запрещено журналировать title, токены, координаты touch,
коды/текст клавиш и пиксели.

## Ограничения версии

- Один display, один touch seat, одно foreground app view.
- Нет split-screen, floating windows, PiP и desktop mode.
- Нет transition animations и сложной gesture arbitration.
- Нет IME/text input, virtual keyboard и accessibility service.
- Persisted task stack и process freezing принадлежат shell/appserv и не
  определены windowserv v1.
