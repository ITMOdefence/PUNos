# GUI Toolkit

Toolkit строится поверх `uilib` и отвечает за navigation stack, widget tree,
адаптивный layout, touch gesture dispatch, theme и accessibility. Он не
является системным сервисом: состояние экранов и виджетов находится в процессе
приложения.

Первый этап реализации включает `App`, `Screen`, navigation stack, safe-area
container, row/column layout, label, touch button и прокручиваемый список.
Минимальный touch target — 48 логических единиц; layout обязан корректно
работать при portrait/landscape configure и разных safe insets. Text field
добавляется вместе с IME-контрактом.

Toolkit распознаёт tap, long press, pan и scroll только внутри touch sequence,
назначенной view. Системные Home/Back gestures принадлежат shell policy.
