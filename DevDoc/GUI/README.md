# Графическая подсистема PUNos

Этот раздел принадлежит GUI-команде и является точкой входа в документацию
графического стека. Общесистемные механизмы остаются в своих каталогах и
подключаются только через публичные контракты.

## Слои

| Слой | Документы | Ответственность |
| --- | --- | --- |
| [Common](Common/) | [Архитектура](Common/Architecture.md), [базовый протокол](Common/protocol-base.md), [общие типы](Common/common-types.md), [владение](Common/ownership.md) | Общие границы, идентификаторы и правила взаимодействия слоёв |
| [Display](Display/) | [displayserv](Display/displayserv.md), [displaylib](Display/displaylib.md) | Физические дисплеи, режимы и показ готового кадра |
| [Compositor](Compositor/) | [compositorserv](Compositor/compositorserv.md), [протокол поверхностей](Compositor/surface-protocol.md), [передача буферов](Compositor/buffer-sharing.md) | Поверхности, графические буферы, damage и композиция сцены |
| [Windowing](Windowing/) | [windowserv](Windowing/windowserv.md), [маршрутизация ввода](Windowing/input-routing.md) | Окна, фокус, размещение, z-order и доставка ввода |
| [Toolkit](Toolkit/) | [uilib](Toolkit/uilib.md), [состав toolkit](Toolkit/README.md) | Клиентский UI API, виджеты, layout, темы и accessibility |

Документы сгруппированы по архитектурным слоям, а не по типу компонента.
Сервис, его библиотека и связанные протоколы находятся рядом. Общие правила
не дублируют специализированные контракты.

## Порядок запуска

```text
logserv
  ├── displayserv
  │     └── compositorserv
  │            └── windowserv
  └── IOserv ──────────┘
                         └── uilib / приложения
```

`IOserv` не является частью GUI: он публикует физические события. Выбор окна,
фокус и преобразование координат принадлежат `windowserv`.

## Правило границы слоёв

GUI может зависеть от публичных заголовков System Libs, `appframework`,
`ipclib` и IPC-контрактов системных сервисов. GUI не подключает внутренние
заголовки и не принимает владение их файловыми дескрипторами или состоянием.
