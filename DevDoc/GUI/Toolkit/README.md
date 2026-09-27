# GUI Toolkit

Toolkit строится поверх `uilib` и отвечает за widget tree, layout, event
propagation, focus traversal, theme и accessibility. Он не является системным
сервисом: состояние виджетов находится в процессе приложения.

Первый этап реализации включает контейнеры layout, label, button, text field,
checkbox, radio button, menu и dialog. Подробные контракты будут извлекаться из
`../Specification.md` после стабилизации сервисных протоколов.
