# windowserv

## Назначение

`windowserv` управляет окнами, рабочей областью, фокусом и маршрутизацией
ввода. Пиксельные данные находятся в `compositorserv` и ему не доступны.

| Поле | Значение |
| --- | --- |
| Идентификатор | `windowserv` |
| Исполняемый файл | `windowserv` |
| Владелец запуска | `initserv` |
| IPC endpoint | `/run/punos/windowserv.sock` |
| Версия протокола | `v1` |

## Зависимости

| Зависимость | Использование |
| --- | --- |
| `compositorserv` | Привязка surface, геометрия, видимость и z-order |
| `IOserv` | Подписка на keyboard, pointer и touch InputFrame |
| `special/ipc` | Клиентские соединения и сервисные подписки |
| `sys/event`, `sys/math` | Event loop, геометрия и hit testing |
| `logserv` | Жизненный цикл, протокольные и policy-ошибки |

## Ответственность

- Владеет `WindowId`, деревом transient/modal связей и workspace.
- Определяет положение, размер, видимость, z-order и активное окно.
- Поддерживает keyboard focus, pointer focus, capture и touch ownership.
- Передаёт scene policy в `compositorserv`.
- Не читает `/dev/input`, не рисует декорации и не импортирует buffer FD.

## Операции

| Операция | Назначение |
| --- | --- |
| `window.create` | Создаёт окно для принадлежащего клиенту `SurfaceId` |
| `window.destroy` | Удаляет окно и все server-side связи |
| `window.show` / `window.hide` | Map/unmap окна |
| `window.set_title` | Обновляет UTF-8 заголовок |
| `window.request_state` | Просит minimize, maximize, restore или fullscreen |
| `window.ack_configure` | Подтверждает полученный размер и состояние |
| `window.begin_move` | Начинает интерактивное перемещение после input serial |
| `window.begin_resize` | Начинает resize указанной грани после input serial |
| `window.set_constraints` | min/max размер клиентской области |
| `window.set_modal` | Устанавливает модального родителя |

```text
WindowCreateRequest = {
  u16 protocol_version, u64 request_id, SurfaceId surface,
  WindowRole role, WindowId parent, Size preferred_size,
  u32 flags, u16 title_length, u8 title[title_length]
}

WindowConfigureEvent = {
  WindowId window, Serial serial, Rect geometry,
  WindowState state, Scale scale, DisplayId display
}
```

Создание возвращает `WindowId`, но клиент не рисует до первого configure.
Новый размер считается принятым только после `window.ack_configure` и commit
поверхности с тем же `Serial`.

## События окна

| Событие | Назначение |
| --- | --- |
| `window.configure` | Новый размер, состояние, scale или display |
| `window.focus_changed` | Изменение keyboard focus |
| `window.close_requested` | Пользователь запросил закрытие |
| `window.visibility_changed` | Окно map, unmap или occluded |
| `window.pointer_enter/leave/motion/button/axis` | Pointer-события в локальных координатах |
| `window.key` | Код клавиши, состояние и modifiers |
| `window.touch_down/move/up/cancel` | Touch sequence окна |

Каждое событие ввода содержит `Serial` и монотонный timestamp. Serial события
требуется для операций, меняющих глобальное состояние по инициативе клиента,
например move, resize или popup. Повторное использование старого serial
возвращает `CORE_PERMISSION_DENIED`.

## Политика v1

1. Клик активирует верхнее фокусируемое окно под указателем.
2. Активное обычное окно поднимается над другими обычными окнами.
3. Always-on-top и системная оболочка располагаются в отдельных слоях.
4. Модальное окно блокирует ввод в цепочку родителей.
5. Popup располагается относительно родителя и закрывается при потере grab.
6. Minimized окно unmapped, но сохраняет объект и последнее размещение.
7. Fullscreen занимает один display и располагается выше обычных окон.
8. Клиент не задаёт абсолютный z-index.

## Состояния

```text
Created → Configured → Visible ↔ Hidden
                       ├→ Minimized
                       ├→ Maximized
                       └→ Fullscreen
любое живое состояние → Closing → Destroyed
```

`close_requested` является просьбой. Если клиент не отвечает в пределах
политики оболочки, принудительное завершение выполняет `appserv`, а не
`windowserv`.

## Отказы и владение

Окно принадлежит IPC-соединению. При отключении клиента его окна удаляются,
focus пересчитывается, grabs отменяются, а scene bindings уничтожаются.
После перезапуска `IOserv` сервис переподключается и сбрасывает состояние
клавиш, pointer capture и touch sequences. После перезапуска compositor все
окна остаются логически живы, но становятся hidden до повторной привязки
surface клиентом.

`windowserv` v1 однопоточный. IPC, InputFrame, hit testing и изменения policy
последовательно выполняются владельцем event loop.

## Ошибки и ограничения v1

- `CORE_INVALID_ARGUMENT`: неверная роль, размер, UTF-8 или payload.
- `CORE_PERMISSION_DENIED`: чужое окно/surface или недействительный serial.
- `CORE_NOT_FOUND`: объект уже удалён или неизвестен.
- `CORE_INVALID_STATE`: операция запрещена ролью или текущим состоянием.
- `CORE_UNSUPPORTED`: неизвестная версия или флаг.
- `CORE_WOULD_BLOCK`: исходящая очередь клиента переполнена.

В v1 нет виртуальных рабочих столов, server-side decorations, clipboard,
drag-and-drop, IME, global shortcuts и достоверной peer identity. Эти
возможности оформляются отдельными расширениями протокола.
