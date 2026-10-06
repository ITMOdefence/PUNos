# Маршрутизация ввода

Нормативный wire format событий `window.pointer`, `window.key` и
`window.touch` определён в [оконном протоколе](window-protocol.md). Этот
документ описывает политику выбора получателя.

## Граница с системным слоем

`IOserv` публикует `IoInputFrameEvent` без преобразования кодов устройств.
`windowserv` подписывается на классы keyboard, pointer и touch и является
единственным GUI-компонентом, который выбирает получателя события.

```text
IOserv InputFrame
  → нормализация координат и состояния в windowserv
  → hit testing / keyboard focus / pointer capture
  → WindowInputEvent
  → uilib
  → widget event dispatch
```

## Touch-first маршрутизация

1. Touch sequence закрепляется за верхним допустимым получателем первого
   `DOWN` до `UP` или `CANCEL`; смена foreground не переносит sequence.
2. Системные gesture regions имеют приоритет только для жестов, объявленных
   shell policy. Одна sequence не принадлежит двум получателям одновременно.
3. Модальный dialog получает ввод раньше app view и блокирует parent.
4. Lock screen получает весь обычный ввод, пока активен.
5. Экранная клавиатура получает touch внутри своей поверхности, а text input
   доставляется view с text focus отдельным IME-протоколом после v1.
6. Pointer применяется в эмуляторе как одно касание либо как pointer event по
   capability клиента. Нажатие pointer не меняет foreground само по себе.
7. Аппаратные key events получает foreground view; Home, Power и Volume
   обрабатывает shell policy и обычному приложению не отдаёт.
8. Background, hidden, suspended и closing view обычный ввод не получают.
9. `windowserv` учитывает orientation и переводит физические координаты в
   локальные логические координаты viewport.
10. `uilib` выполняет hit testing и распознавание widget-жестов внутри view.

## Разделение ответственности

`IOserv` не знает о view, gesture regions и foreground. `windowserv` не
преобразует клавиши в текст и не реализует произвольные widget-жесты. Toolkit
распознаёт tap, long press, pan и scroll из уже назначенной приложению touch
sequence. Текстовый ввод, compose и экранная клавиатура оформляются отдельным
IME-протоколом после v1. Toolkit не получает raw device id.
