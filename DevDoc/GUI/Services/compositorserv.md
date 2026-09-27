# compositorserv

## Назначение

`compositorserv` принимает поверхности приложений, импортирует графические
буферы, собирает scene graph и передаёт итоговый кадр в `displayserv`.

| Поле | Значение |
| --- | --- |
| Идентификатор | `compositorserv` |
| Исполняемый файл | `compositorserv` |
| Владелец запуска | `initserv` |
| IPC endpoint | `/run/punos/compositorserv.sock` |
| Версия протокола | `v1` |

## Зависимости

| Зависимость | Использование |
| --- | --- |
| `displayserv` | Выходы, конфигурация, present и vsync |
| `special/ipc` | Клиенты и привилегированный scene-control канал |
| `sys/event` | IPC и frame scheduling |
| `sys/memory`, `sys/math` | Буферы, регионы, преобразования и clipping |
| `logserv` | Ошибки клиентов, кадров и восстановления |

## Ответственность

- Владеет `SurfaceId`, импортированными `BufferId` и scene graph.
- Применяет pending surface state атомарно по `surface.commit`.
- Отслеживает damage, непрозрачность и видимость.
- Отдаёт клиенту буфер только после прекращения чтения.
- Не выбирает фокус, положение окна и оконную политику.
- Не рисует виджеты и не интерпретирует ввод.

## Клиенты

| Клиент | Доступ |
| --- | --- |
| `uilib` | surfaces, buffers, damage, commit и frame callback |
| `windowserv` | размещение, видимость, clipping и z-order surfaces |

Обычный клиент изменяет только принадлежащие ему поверхности и буферы.
Scene-control операции разрешены только соединению `windowserv`.

## Операции клиента

| Операция | Назначение |
| --- | --- |
| `surface.create` | Создаёт невидимую поверхность клиента |
| `surface.destroy` | Удаляет поверхность и pending state |
| `buffer.import` | Импортирует shared-memory FD и возвращает `BufferId` |
| `buffer.destroy` | Удаляет неиспользуемый буфер |
| `surface.attach` | Устанавливает pending buffer или detach |
| `surface.damage` | Добавляет прямоугольники к pending damage |
| `surface.set_opaque_region` | Задаёт гарантированно непрозрачную область |
| `surface.request_frame` | Запрашивает callback после кадра |
| `surface.commit` | Атомарно публикует pending state |

```text
BufferImportRequest = {
  u16 protocol_version, u64 request_id,
  u32 width, u32 height, u32 stride,
  PixelFormat format, u64 byte_size, received fd
}

SurfaceCommitRequest = {
  u16 protocol_version, u64 request_id,
  SurfaceId surface, Serial commit_serial
}
```

Attach, damage и opaque region накапливаются до commit. Ошибка commit не
изменяет current state поверхности.

## Scene-control операции

| Операция | Назначение |
| --- | --- |
| `scene.bind_window` | Связывает `WindowId` с принадлежащим клиенту `SurfaceId` |
| `scene.set_geometry` | Положение, размер и clipping в глобальной сцене |
| `scene.set_visibility` | Map/unmap поверхности |
| `scene.set_stack` | Порядок поверхности внутри workspace |
| `scene.set_output` | Назначает основной дисплей поверхности |
| `scene.remove_window` | Удаляет связь окна и поверхности |

`windowserv` передаёт compositor только политику сцены. Он не получает доступ
к содержимому shared-memory буфера.

## События

| Событие | Получатель | Назначение |
| --- | --- | --- |
| `surface.frame_done` | владелец surface | Commit участвовал в показанном кадре |
| `surface.buffer_released` | владелец buffer | Буфер снова разрешено изменять |
| `surface.output_enter` | владелец surface | Surface появилась на дисплее |
| `surface.output_leave` | владелец surface | Surface покинула дисплей |
| `surface.full_redraw` | владелец surface | Содержимое потеряно; нужен полный commit |
| `scene.error` | `windowserv` | Scene-control операция отклонена |

`frame_done` не гарантирует, что буфер освобождён: один буфер может ещё
использоваться последующим кадром. Освобождение подтверждает только
`buffer_released`.

## Композиция

Сервис планирует кадр при новом commit, изменении сцены или display event.
Damage клиента ограничивается размером поверхности и преобразуется в
координаты дисплея. Непрозрачные области позволяют исключать закрытые регионы.
При переполнении region список схлопывается в bounding rectangle.

## Владение и состояния

```text
Surface: Created → Mapped ↔ Unmapped → Destroyed
Buffer:  Imported → Attached → InUse → Released → Destroyed
```

Отключение клиента уничтожает все его surfaces и закрывает импортированные FD.
Буфер в `InUse` удаляется логически сразу, а FD закрывается после завершения
последнего кадра. Все операции выполняются в одном event-loop потоке;
рендеринг может быть вынесен в worker только в последующей версии.

## Ошибки и ограничения v1

- `CORE_INVALID_ARGUMENT`: неверная геометрия, формат, stride или FD.
- `CORE_PERMISSION_DENIED`: клиент меняет чужой объект или scene-control.
- `CORE_NOT_FOUND`: неизвестный идентификатор.
- `CORE_INVALID_STATE`: buffer уничтожается во время недопустимого состояния.
- `CORE_WOULD_BLOCK`: очередь commits клиента переполнена.
- `CORE_IO_ERROR`: ошибка displayserv, IPC или отображения памяти.

v1 поддерживает только `XRGB8888` и premultiplied `ARGB8888`, axis-aligned
surfaces и software composition. Effects, rotation surface, GPU command
buffers и direct scanout не входят в контракт.
