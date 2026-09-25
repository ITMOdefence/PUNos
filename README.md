# GUI System Specification

> Минимальная спецификация графического интерфейса операционной системы.

**Status:** Draft
**Version:** 0.1
**Scope:** Windowing, Widgets, Input, Rendering, Compositing, Window Management, Theme, Accessibility

---

## 1. Назначение

Данный документ определяет минимальный набор компонентов и API, необходимых для построения единой графической подсистемы операционной системы.

Спецификация описывает:

* визуальные компоненты;
* окна и управление ими;
* базовые виджеты;
* мышь и клавиатуру;
* систему событий;
* rendering и compositing;
* Window Manager;
* Theme API;
* Accessibility API;
* поведение системы при различных состояниях и событиях;
* минимальные требования к UX и визуальной консистентности.

Спецификация намеренно не определяет конкретный графический backend, язык программирования или способ IPC.

---

# 2. Архитектура

GUI состоит из нескольких уровней.

```text
┌─────────────────────────────────────────────┐
│                 Application                 │
├─────────────────────────────────────────────┤
│                 GUI Toolkit                 │
│                                             │
│ Window / Widget / Menu / Dialog / Theme     │
├─────────────────────────────────────────────┤
│                 GUI API                     │
├──────────────────┬──────────────────────────┤
│ Window Manager   │      Input System        │
├──────────────────┴──────────────────────────┤
│                 Compositor                  │
├─────────────────────────────────────────────┤
│              Graphics Backend               │
├─────────────────────────────────────────────┤
│                 GPU / Display               │
└─────────────────────────────────────────────┘
```

### Основные компоненты

| Компонент       | Назначение                                              |
| --------------- | ------------------------------------------------------- |
| `Window`        | Жизненный цикл и состояние окон                         |
| `Widget`        | Базовые элементы интерфейса                             |
| `Event`         | Унифицированная система событий                         |
| `Input`         | Мышь, клавиатура и другие устройства                    |
| `Menu`          | Главное и контекстное меню                              |
| `Surface`       | Графическая поверхность приложения                      |
| `Renderer`      | Отрисовка                                               |
| `Compositor`    | Композиция поверхностей                                 |
| `WindowManager` | Размещение и управление окнами                          |
| `Theme`         | Цвета, шрифты, размеры и оформление                     |
| `Accessibility` | Клавиатурная навигация, масштабирование и accessibility |

---

# 3. Основные принципы

GUI должен соответствовать следующим принципам.

## 3.1. Единообразие

Одинаковые элементы должны вести себя одинаково во всех приложениях.

Например:

* `Button` всегда реагирует на `Enter`/`Space` одинаковым образом;
* `Tab` перемещает фокус;
* `Esc` закрывает модальный диалог, если это разрешено;
* disabled-элемент не принимает пользовательский ввод;
* правый клик вызывает контекстное меню, если оно определено.

## 3.2. Предсказуемость

Каждое пользовательское действие должно иметь понятный результат.

```text
hover  → visual feedback
press  → pressed state
click  → action
focus  → focus indicator
disabled → no action
drag   → drag feedback
```

## 3.3. Разделение ответственности

Приложение отвечает за содержимое.

Window Manager отвечает за окна.

Compositor отвечает за композицию.

Input System отвечает за получение и маршрутизацию ввода.

Renderer отвечает за отрисовку.

---

# 4. Window API

Окно является основным контейнером GUI.

## 4.1. Создание

```text
Window.create()
```

Результатом является объект `Window`.

Минимальные параметры:

```text
WindowConfig
├── title
├── width
├── height
├── position
├── flags
├── icon
└── parent
```

Пример:

```text
window = Window.create({
    title: "My Application",
    width: 800,
    height: 600
})
```

---

## 4.2. Жизненный цикл

```text
window.show()
window.hide()
window.close()
window.destroy()
```

Состояния:

```text
CREATED
VISIBLE
ACTIVE
INACTIVE
MINIMIZED
MAXIMIZED
CLOSING
DESTROYED
```

Переходы:

```text
CREATED
   │
   ▼
VISIBLE
   │
   ├── activate ──► ACTIVE
   │                  │
   │                  ▼
   │              INACTIVE
   │
   ├── minimize ──► MINIMIZED
   │
   └── close ─────► CLOSING ──► DESTROYED
```

---

# 5. Window Controls

Каждое стандартное окно должно поддерживать:

```text
Minimize
Maximize / Restore
Close
```

Если операция недоступна, соответствующая кнопка должна быть визуально disabled.

Например:

```text
┌───────────────────────────────────────┐
│ My Application             _ □  ×     │
├───────────────────────────────────────┤
│                                       │
│              Content                  │
│                                       │
└───────────────────────────────────────┘
```

---

# 6. Window API

```text
Window.create()
Window.destroy()

Window.show()
Window.hide()

Window.minimize()
Window.maximize()
Window.restore()
Window.close()

Window.move(x, y)
Window.resize(width, height)

Window.set_title(text)
Window.set_icon(icon)

Window.set_position(x, y)
Window.set_size(width, height)

Window.set_modal(enabled)
Window.set_parent(window)

Window.get_geometry()
Window.get_state()

Window.activate()
Window.deactivate()
```

---

# 7. Window Events

```text
on_create
on_destroy
on_show
on_hide

on_activate
on_deactivate

on_move
on_resize

on_minimize
on_maximize
on_restore

on_close
```

Перед закрытием окно должно получать:

```text
on_close_request
```

Приложение может:

```text
ALLOW
DENY
DEFER
```

Например, если есть несохранённые изменения:

```text
User clicks ×
       │
       ▼
on_close_request
       │
       ▼
Unsaved changes?
    │        │
   No       Yes
    │        │
    ▼        ▼
 Close    Show dialog
```

---

# 8. Widget System

Все элементы интерфейса должны наследоваться от базового `Widget`.

```text
Widget
├── Label
├── Button
├── TextField
├── CheckBox
├── RadioButton
├── ComboBox
├── List
├── ScrollBar
├── Slider
├── Image
├── Container
└── CustomWidget
```

---

# 9. Widget API

```text
Widget.create(parent)
Widget.destroy()

Widget.show()
Widget.hide()

Widget.enable()
Widget.disable()

Widget.focus()
Widget.blur()

Widget.set_position(x, y)
Widget.set_size(width, height)

Widget.get_position()
Widget.get_size()
Widget.get_bounds()

Widget.set_style(style)
Widget.get_style()

Widget.set_tooltip(text)
```

---

# 10. Widget States

Каждый интерактивный widget должен поддерживать состояния:

```text
NORMAL
HOVER
PRESSED
FOCUSED
DISABLED
SELECTED
CHECKED
ACTIVE
```

Состояния могут комбинироваться.

Например:

```text
BUTTON + HOVER
BUTTON + FOCUSED
BUTTON + PRESSED
BUTTON + DISABLED
```

---

# 11. Button

API:

```text
Button.create(parent)

Button.set_text(text)
Button.set_icon(icon)

Button.set_default(bool)
Button.set_cancel(bool)

Button.on_click(callback)
```

Поведение:

### Mouse

```text
Mouse Enter
    ↓
HOVER

Mouse Down
    ↓
PRESSED

Mouse Up
    ↓
CLICK
```

Если курсор покинул кнопку во время нажатия:

```text
Mouse Down
    ↓
PRESSED
    ↓
Mouse Leave
    ↓
NORMAL
```

Клик не должен происходить.

### Keyboard

Если кнопка находится в фокусе:

```text
Space → PRESSED → CLICK
Enter → CLICK
```

---

# 12. Label

Label является неинтерактивным элементом.

```text
Label.create(parent)
Label.set_text(text)
Label.set_alignment(alignment)
Label.set_font(font)
```

Label не должен получать keyboard focus, если он не используется как accessibility target.

---

# 13. TextField

API:

```text
TextField.create(parent)

TextField.set_text(text)
TextField.get_text()

TextField.set_placeholder(text)

TextField.set_selection(start, end)
TextField.select_all()

TextField.copy()
TextField.cut()
TextField.paste()

TextField.clear()

TextField.on_change()
TextField.on_submit()
```

Поведение:

```text
Click
  ↓
Focus
  ↓
Caret visible
  ↓
Keyboard input
  ↓
Text changes
  ↓
on_change
```

Горячие клавиши:

```text
Ctrl+A → Select All
Ctrl+C → Copy
Ctrl+X → Cut
Ctrl+V → Paste
Ctrl+Z → Undo
```

---

# 14. CheckBox

API:

```text
CheckBox.create(parent)

CheckBox.set_checked(bool)
CheckBox.is_checked()

CheckBox.on_change(callback)
```

Состояния:

```text
UNCHECKED
CHECKED
DISABLED
```

Опционально:

```text
INDETERMINATE
```

При клике:

```text
UNCHECKED → CHECKED
CHECKED   → UNCHECKED
```

---

# 15. RadioButton

Radio buttons должны объединяться в группу.

```text
RadioGroup.create()

RadioButton.set_group(group)
RadioButton.set_selected(bool)
RadioButton.is_selected()

RadioButton.on_change()
```

В одной группе одновременно может быть выбран только один элемент.

---

# 16. ComboBox

API:

```text
ComboBox.create(parent)

ComboBox.add_item(id, text)
ComboBox.remove_item(id)

ComboBox.clear()

ComboBox.select(id)
ComboBox.get_selected()

ComboBox.open()
ComboBox.close()

ComboBox.on_change()
```

Поведение:

```text
Click
  ↓
Open dropdown
  ↓
Select item
  ↓
Close dropdown
  ↓
Update value
  ↓
on_change
```

---

# 17. Menu API

Поддерживаются:

* application menu;
* context menu;
* submenu;
* separator;
* checked item;
* disabled item.

```text
Menu.create()

Menu.add_item(id, text)
Menu.add_separator()
Menu.add_submenu(id, menu)

Menu.remove_item(id)

Menu.set_enabled(id, bool)
Menu.set_checked(id, bool)

Menu.show(x, y)
Menu.hide()

Menu.on_select(callback)
```

---

# 18. Context Menu

Правый клик:

```text
Mouse Right Click
        ↓
Check context
        ↓
Context Menu
        ↓
Show at cursor position
```

Контекстное меню не должно выходить за границы экрана.

Если меню не помещается справа:

```text
┌───────────────┐
│               │
│           ┌───┤
│           │ M │
│           │ E │
│           │ N │
│           │ U │
│           └───┤
└───────────────┘
```

оно должно автоматически раскрываться в противоположную сторону.

---

# 19. Input System

Input System получает физический ввод и преобразует его в GUI events.

Поддерживаются:

```text
Mouse
Keyboard
Touch
Gamepad (optional)
```

---

# 20. Mouse Events

Минимальный набор:

```text
mouse_move
mouse_down
mouse_up
mouse_double_click
mouse_enter
mouse_leave
mouse_wheel
```

Событие:

```text
MouseEvent
├── type
├── x
├── y
├── button
├── buttons
└── modifiers
```

---

# 21. Keyboard Events

```text
key_down
key_up
text_input
```

```text
KeyboardEvent
├── key
├── modifiers
├── repeat
└── scancode
```

---

# 22. Focus

В каждый момент времени должен существовать:

```text
Active Window
Focused Widget
```

Например:

```text
Desktop
 └── Window A
      └── TextField ← keyboard focus
```

API:

```text
Focus.set(widget)
Focus.get()
Focus.clear()

Focus.next()
Focus.previous()
```

---

# 23. Tab Navigation

`Tab` должен перемещать фокус между focusable widgets.

```text
Button A
   ↓ Tab
TextField
   ↓ Tab
Button B
   ↓ Tab
CheckBox
```

`Shift+Tab` перемещает фокус назад.

Каждый widget должен иметь:

```text
focusable: bool
tab_index: integer
```

---

# 24. Shortcut API

```text
Shortcut.register(
    modifiers,
    key,
    callback
)

Shortcut.unregister(...)
```

Примеры:

```text
Ctrl+S → Save
Ctrl+O → Open
Ctrl+Q → Quit
Ctrl+Z → Undo
Ctrl+Shift+Z → Redo
```

Системные shortcuts имеют более высокий приоритет, чем application shortcuts.

---

# 25. Event Propagation

События проходят через три стадии:

```text
Capture
   ↓
Target
   ↓
Bubble
```

Пример:

```text
Window
  ↓
Container
  ↓
Button ← target
```

Событие может быть остановлено:

```text
event.stop_propagation()
```

---

# 26. Drag & Drop

API:

```text
Drag.start(data)
Drag.enter(target)
Drag.over(target)
Drag.leave(target)
Drag.drop(target)
Drag.end()
```

Состояния:

```text
IDLE
DRAGGING
OVER_TARGET
DROPPED
CANCELLED
```

При наведении объекта на допустимый target:

```text
DRAGGING
    ↓
OVER_TARGET
    ↓
Visual feedback
```

При недопустимом target:

```text
NOT_ALLOWED cursor
```

---

# 27. Cursor API

```text
Cursor.set(type)
Cursor.get()

Cursor.show()
Cursor.hide()

Cursor.set_custom(image)
```

Минимальные типы:

```text
DEFAULT
POINTER
TEXT
WAIT
CROSSHAIR
MOVE

RESIZE_HORIZONTAL
RESIZE_VERTICAL
RESIZE_DIAGONAL

GRAB
GRABBING

NOT_ALLOWED
```

Курсор должен автоматически изменяться в зависимости от текущего interaction state.

---

# 28. Surface API

Приложение не должно напрямую управлять физическим framebuffer.

Оно работает с `Surface`.

```text
Surface.create(width, height)
Surface.destroy()

Surface.lock()
Surface.unlock()

Surface.present()

Surface.set_damage(region)
Surface.get_buffer()
```

---

# 29. Renderer API

Минимальный Renderer:

```text
Renderer.clear()
Renderer.draw_rect()
Renderer.draw_round_rect()

Renderer.draw_line()
Renderer.draw_image()
Renderer.draw_text()

Renderer.clip()
Renderer.save()
Renderer.restore()

Renderer.translate()
Renderer.scale()
Renderer.rotate()
```

---

# 30. Compositor

Compositor отвечает за объединение всех окон.

```text
Compositor.create_surface(window)
Compositor.destroy_surface(surface)

Compositor.set_position(surface, x, y)
Compositor.set_size(surface, width, height)

Compositor.set_opacity(surface, opacity)

Compositor.show(surface)
Compositor.hide(surface)

Compositor.raise(surface)
Compositor.lower(surface)

Compositor.commit(surface)
```

---

# 31. Z-Order

Каждая surface имеет Z-order.

```text
Highest Z
────────────
Modal dialog
Application window
Application window
Desktop
────────────
Lowest Z
```

Активное окно должно находиться перед обычными окнами своего слоя.

При активации:

```text
click window
    ↓
activate
    ↓
raise
    ↓
focus
    ↓
visual update
```

---

# 32. Modal Windows

Modal window блокирует interaction с родительским окном.

```text
Application Window
        │
        ▼
┌─────────────────────┐
│     Modal Dialog    │
│                     │
│   Cancel    Save    │
└─────────────────────┘
```

Пока modal dialog открыт:

```text
Parent mouse input → blocked
Parent keyboard → blocked
Dialog input → allowed
```

После закрытия:

```text
Dialog closes
     ↓
Parent becomes active
     ↓
Previous focus restored
```

---

# 33. Window Manager

Window Manager отвечает за:

* размещение окон;
* активацию;
* фокус;
* minimize/maximize;
* tiling;
* stacking;
* workspaces;
* multi-monitor.

API:

```text
WindowManager.create_window()
WindowManager.destroy_window()

WindowManager.activate(window)
WindowManager.focus(window)

WindowManager.move(window, x, y)
WindowManager.resize(window, w, h)

WindowManager.minimize(window)
WindowManager.maximize(window)
WindowManager.restore(window)

WindowManager.raise(window)
WindowManager.lower(window)

WindowManager.get_active_window()
WindowManager.get_window_list()
```

---

# 34. Window Placement

При создании окна Window Manager должен определить его положение.

Если приложение не указало позицию:

```text
Window created
      ↓
WM calculates position
      ↓
Window placed inside work area
```

Окно не должно автоматически создаваться за пределами доступного рабочего пространства.

---

# 35. Multi-Monitor

Каждый monitor предоставляет:

```text
Monitor
├── id
├── resolution
├── scale
├── refresh_rate
└── work_area
```

API:

```text
Display.get_monitors()
Display.get_primary_monitor()

Monitor.get_geometry()
Monitor.get_scale()
Monitor.get_work_area()
```

При перемещении окна между мониторами GUI должен учитывать scale каждого монитора.

---

# 36. Theme API

Theme отвечает за визуальный стиль.

```text
Theme.get_color(name)
Theme.get_font(name)
Theme.get_size(name)
Theme.get_spacing(name)
Theme.get_radius(name)
Theme.get_icon(name)
```

---

# 37. Цветовая схема

Минимальные semantic colors:

```text
background
surface
surface_hover
surface_pressed

text
text_secondary
text_disabled

accent
accent_hover
accent_pressed

border
focus

success
warning
error
info
```

Виджеты не должны напрямую зависеть от конкретных RGB значений.

Нужно использовать semantic tokens:

```text
Button.background = Theme.color("accent")
```

а не:

```text
Button.background = #3478F6
```

---

# 38. Typography

Theme должен определять:

```text
font_family
font_size
font_weight
line_height
letter_spacing
```

Минимальные роли:

```text
body
body_small
caption
heading
title
button
monospace
```

---

# 39. Spacing

Все стандартные виджеты должны использовать единую систему размеров.

Например:

```text
spacing-xs
spacing-sm
spacing-md
spacing-lg
spacing-xl
```

Размеры должны быть определены Theme.

Приложение не должно самостоятельно создавать произвольную сетку отступов для стандартных элементов.

---

# 40. Accessibility

Минимальный Accessibility API:

```text
Accessibility.set_scale(scale)
Accessibility.get_scale()

Accessibility.set_text_scale(scale)

Accessibility.set_high_contrast(enabled)
Accessibility.set_reduced_motion(enabled)
```

Widget:

```text
Widget.set_accessible_name()
Widget.set_accessible_description()
Widget.set_accessible_role()
Widget.get_accessible_state()
```

Roles:

```text
window
button
text_field
checkbox
radio
menu
menu_item
list
list_item
slider
image
dialog
```

---

# 41. DPI и Scaling

GUI должен работать независимо от физического DPI.

Используется logical coordinate system:

```text
logical pixels
        ↓
display scale
        ↓
physical pixels
```

Например:

```text
100 logical px
×
2.0 scale
=
200 physical px
```

API:

```text
Display.get_scale()
Window.get_scale()
Widget.get_scale()
```

---

# 42. System Feedback

Каждое состояние должно иметь визуальную обратную связь.

| Состояние    | Реакция                     |
| ------------ | --------------------------- |
| Hover        | изменение background/border |
| Pressed      | pressed appearance          |
| Focused      | focus ring                  |
| Disabled     | reduced contrast            |
| Selected     | selected appearance         |
| Checked      | check indicator             |
| Dragging     | cursor + preview            |
| Invalid drop | `NOT_ALLOWED` cursor        |
| Loading      | progress indicator          |
| Error        | error state                 |
| Success      | success feedback            |

---

# 43. Disabled Elements

Disabled widget:

```text
widget.disable()
```

должен:

* перестать принимать обычный input;
* не реагировать на click;
* не реагировать на keyboard activation;
* иметь disabled visual state;
* оставаться доступным для accessibility tree.

Пример:

```text
NORMAL
  ↓ disable()
DISABLED
```

---

# 44. Hover

При попадании курсора:

```text
mouse_enter
    ↓
HOVER
```

При выходе:

```text
mouse_leave
    ↓
NORMAL
```

Hover не должен менять layout.

---

# 45. Focus

При получении focus:

```text
widget.focus()
    ↓
FOCUSED
    ↓
draw focus indicator
```

При потере:

```text
widget.blur()
    ↓
NORMAL
```

Focus indicator должен быть видимым и не зависеть только от изменения цвета.

---

# 46. Resize

При изменении размера окна:

```text
Window resize
      ↓
on_resize
      ↓
Layout recalculation
      ↓
Widget layout
      ↓
Surface resize
      ↓
Redraw
      ↓
Compositor commit
```

Если приложение не успевает отрисовать новый frame, compositor должен продолжать показывать предыдущий валидный frame, если это возможно.

---

# 47. Redraw / Damage

GUI не должен перерисовывать весь экран при каждом изменении.

Каждая surface должна иметь damage region:

```text
Surface
└── Damage Region
      ├── x
      ├── y
      ├── width
      └── height
```

При изменении кнопки:

```text
Button changed
    ↓
Mark button region dirty
    ↓
Render only affected region
    ↓
Commit
```

---

# 48. Application Lifecycle

Минимальный lifecycle приложения:

```text
STARTING
   ↓
RUNNING
   ↓
BACKGROUND
   ↓
CLOSING
   ↓
STOPPED
```

GUI должен корректно обрабатывать:

```text
application crash
application hang
window destroy
surface disconnect
```

При аварийном завершении приложения:

```text
Application crash
      ↓
Detect disconnect
      ↓
Destroy windows
      ↓
Destroy surfaces
      ↓
Remove from Window Manager
      ↓
Release resources
```

Другие приложения не должны зависеть от GUI surface аварийно завершившегося приложения.

---

# 49. Event Loop

Каждое GUI-приложение должно иметь event loop.

```text
while running:

    event = EventQueue.wait()

    dispatch(event)

    update()

    render()

    commit()
```

API:

```text
EventQueue.push(event)
EventQueue.poll()
EventQueue.wait()

Application.run()
Application.quit()
```

---

# 50. Event Queue

Event queue должна гарантировать порядок событий в пределах одного input source.

Пример:

```text
mouse_down
mouse_up
click
```

не должен превращаться в:

```text
click
mouse_down
mouse_up
```

---

# 51. System-Level Events

GUI должен поддерживать системные события:

```text
display_connected
display_disconnected

theme_changed
scale_changed

system_sleep
system_wake

window_manager_changed
```

Приложение может подписываться:

```text
System.on("theme_changed", callback)
System.on("display_connected", callback)
```

---

# 52. Theme Change

При изменении темы:

```text
Theme changed
    ↓
theme_changed event
    ↓
Invalidate GUI
    ↓
Recalculate styles
    ↓
Redraw
    ↓
Compositor commit
```

Приложение не должно самостоятельно перезапускаться.

---

# 53. Display Scale Change

При изменении scale:

```text
Scale changed
    ↓
Display event
    ↓
Window recalculation
    ↓
Widget metrics update
    ↓
Surface resize
    ↓
Redraw
```

---

# 54. Error Handling

API не должен молча игнорировать ошибки.

Минимальные ошибки:

```text
INVALID_ARGUMENT
INVALID_STATE
PERMISSION_DENIED

RESOURCE_NOT_FOUND
RESOURCE_EXHAUSTED

WINDOW_NOT_FOUND
SURFACE_NOT_FOUND
WIDGET_NOT_FOUND

NOT_SUPPORTED
ALREADY_EXISTS
BUSY
```

Пример:

```text
window.maximize()
```

может вернуть:

```text
OK
INVALID_STATE
NOT_SUPPORTED
```

---

# 55. Threading

GUI objects должны иметь определённый ownership model.

Базовый вариант:

```text
GUI Thread
    │
    ├── Window
    ├── Widget
    ├── Event Loop
    └── Renderer
```

Из других потоков GUI напрямую изменять нельзя.

Вместо этого:

```text
Worker Thread
      ↓
post_event()
      ↓
GUI Thread
      ↓
Widget modification
```

API:

```text
Application.post(callback)
```

---

# 56. Security Boundary

Приложение не должно иметь прямого доступа к:

* framebuffer других приложений;
* surfaces других приложений;
* Window Manager internals;
* системному input stream;
* чужим окнами без соответствующего разрешения.

Архитектурно:

```text
Application
     │
     │ GUI API
     ▼
GUI Service
     │
     ├── Window Manager
     ├── Compositor
     └── Input
```

---

# 57. Минимальный Public API

Публичный API для обычного приложения должен выглядеть примерно так:

```text
Application
Window
Widget

Label
Button
TextField
CheckBox
RadioButton
ComboBox
Menu

Event
Input

Theme
Accessibility
```

Пример:

```text
app = Application.create()

window = Window.create({
    title: "Example",
    width: 800,
    height: 600
})

button = Button.create(window)

button.set_text("Click me")

button.on_click(() => {
    print("Hello")
})

window.show()

app.run()
```

---

# 58. System API

Низкоуровневый системный API:

```text
WindowManager
Compositor
Surface
Renderer
Display
Input
```

Эти API могут быть доступны через IPC.

Пример:

```text
Application
     │
     │ IPC
     ▼
GUI Server
     │
     ├── Window Manager
     ├── Compositor
     ├── Input Server
     └── Display Server
```

---

# 59. Минимальный IPC Protocol

Если GUI реализован отдельным системным сервисом, минимальный набор сообщений:

```text
CREATE_WINDOW
DESTROY_WINDOW

SHOW_WINDOW
HIDE_WINDOW

MOVE_WINDOW
RESIZE_WINDOW

ACTIVATE_WINDOW
DEACTIVATE_WINDOW

MINIMIZE_WINDOW
MAXIMIZE_WINDOW
RESTORE_WINDOW

CREATE_SURFACE
DESTROY_SURFACE

COMMIT_SURFACE

SET_CURSOR
SET_TITLE
SET_ICON
```

Input:

```text
MOUSE_MOVE
MOUSE_DOWN
MOUSE_UP
MOUSE_WHEEL

KEY_DOWN
KEY_UP
TEXT_INPUT
```

System:

```text
THEME_CHANGED
SCALE_CHANGED
DISPLAY_CONNECTED
DISPLAY_DISCONNECTED
```

---

# 60. Recommended Directory Structure

Реализацию можно организовать следующим образом:

```text
gui/
├── core/
│   ├── application
│   ├── event
│   ├── event_queue
│   └── object
│
├── window/
│   ├── window
│   ├── window_manager
│   └── dialog
│
├── widget/
│   ├── widget
│   ├── button
│   ├── label
│   ├── text_field
│   ├── checkbox
│   ├── radio_button
│   ├── combo_box
│   ├── list
│   └── container
│
├── input/
│   ├── keyboard
│   ├── mouse
│   ├── cursor
│   └── focus
│
├── menu/
│   ├── menu
│   ├── menu_item
│   └── context_menu
│
├── graphics/
│   ├── surface
│   ├── renderer
│   ├── compositor
│   └── display
│
├── theme/
│   ├── theme
│   ├── colors
│   ├── fonts
│   └── metrics
│
├── accessibility/
│   ├── accessibility
│   └── accessibility_node
│
└── ipc/
    ├── protocol
    ├── client
    └── server
```

---

# 61. Основные правила поведения

GUI implementation должен соблюдать следующие правила.

### Rule 1 — Input

```text
Disabled widget
→ no action
```

### Rule 2 — Focus

```text
Only one widget
can own keyboard focus
within an active window.
```

### Rule 3 — Active Window

```text
Only one top-level window
is active on a workspace.
```

### Rule 4 — Modal

```text
Modal window
blocks interaction
with its parent.
```

### Rule 5 — Z-order

```text
Focused window
must be above ordinary
inactive windows of the same layer.
```

### Rule 6 — Accessibility

```text
Every interactive widget
must have an accessible role
and name.
```

### Rule 7 — Scaling

```text
No GUI component
may depend directly
on physical pixels.
```

### Rule 8 — Theme

```text
Standard widgets
must use Theme tokens
instead of hardcoded colors.
```

### Rule 9 — Rendering

```text
Applications render into
their own Surface.
```

### Rule 10 — Isolation

```text
Application cannot access
another application's Surface.
```

---

# 62. Definition of Done

GUI subsystem считается соответствующим данной спецификации, если он поддерживает:

* [ ] создание и уничтожение окон;
* [ ] minimize/maximize/restore/close;
* [ ] active/inactive windows;
* [ ] Z-order;
* [ ] mouse input;
* [ ] keyboard input;
* [ ] focus;
* [ ] Tab navigation;
* [ ] shortcuts;
* [ ] drag-and-drop;
* [ ] Button;
* [ ] Label;
* [ ] TextField;
* [ ] CheckBox;
* [ ] RadioButton;
* [ ] ComboBox;
* [ ] Menu;
* [ ] Context Menu;
* [ ] Cursor;
* [ ] Surface;
* [ ] Renderer;
* [ ] Compositor;
* [ ] Window Manager;
* [ ] Theme;
* [ ] DPI/scale;
* [ ] accessibility metadata;
* [ ] visual states;
* [ ] event propagation;
* [ ] damage-based redraw;
* [ ] application crash cleanup;
* [ ] GUI IPC;
* [ ] multi-monitor support.

---

# 63. Итоговая модель

В конечном виде GUI должен выглядеть как система из трёх основных уровней:

```text
             APPLICATION
                  │
                  ▼
        ┌──────────────────┐
        │    GUI TOOLKIT   │
        │                  │
        │ Window           │
        │ Widget           │
        │ Menu             │
        │ Event            │
        │ Theme            │
        │ Accessibility    │
        └────────┬─────────┘
                 │
                 ▼
             GUI IPC
                 │
       ┌─────────┴─────────┐
       │                   │
       ▼                   ▼
 WINDOW MANAGER       INPUT SYSTEM
       │                   │
       └─────────┬─────────┘
                 ▼
             COMPOSITOR
                 │
                 ▼
              SURFACE
                 │
                 ▼
             RENDERER
                 │
                 ▼
            GPU / DISPLAY
```

Главная идея архитектуры:

**Application описывает, что должно быть показано.
GUI Toolkit предоставляет удобный API.
Window Manager решает, где находится окно.
Input System решает, куда направить ввод.
Renderer рисует содержимое.
Compositor собирает окна в итоговый кадр.
Display Server выводит кадр на экран.**

Эти границы должны сохраняться независимо от конкретной реализации graphics backend.
