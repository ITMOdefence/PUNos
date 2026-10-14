# Решения по camserv и IOserv v1

Источник вопросов — раздел 9 `ТЗ_camserv_IOserv.md`. Исходные документы не
определяли перечисленные ниже значения. В этой задаче исполнитель предложил
единый минимальный вариант для B-01–B-07, после чего пользователь ответил
«Реализовывай». Это согласование относится к данной реализации; таблица не
выдаёт выбранные значения за требования исходных документов.

| ID | Согласованное решение | Где зафиксирован контракт |
| --- | --- | --- |
| B-01 | Типы camera 1–10 в порядке get_info/request, get_info_result, subscribe, subscribe_result, unsubscribe, unsubscribe_result, ready, frame, device_lost, error. На неизвестный или серверный тип от клиента — camera.error с CORE_INVALID_ARGUMENT. | `abilities/camserv.md`, таблица message_type |
| B-02 | get_info без payload; state 0=Unavailable, 1=Streaming; config — пять u32 little-endian: format, width, height, fps, max_frame_bytes; format 1=Yuyv, 2=Mjpeg, 3=Nv12; неопределённая config заполнена нулями. | `abilities/camserv.md`, wire-контракт |
| B-03 | camera.device_lost передаёт reason=CORE_IO_ERROR. | `abilities/camserv.md`, wire-контракт |
| B-04 | Неподдерживаемая версия camera.subscribe — CORE_UNSUPPORTED; неверный размер payload — CORE_INVALID_ARGUMENT. | `abilities/camserv.md`, wire-контракт |
| B-05 | Ответ на неизвестный или серверный тип от клиента IOserv — io.error, message_type=8, payload u32 CORE_INVALID_ARGUMENT. | `base/IOserv.md`, таблица message_type |
| B-06 | service_id и имя ELF — `IOserv`; endpoint — `/run/punos/ioserv.sock`. | `base/IOserv.md`, раздел имени сервиса |
| B-07 | Целевой клиент будущей GUI-интеграции — windowserv; в этой ветке сервер проверяется независимым IPC-клиентом. Интеграция windowserv остаётся отдельной работой. | `base/IOserv.md`, раздел клиентов |

Автотесты проверяют реализацию выбранного контракта, но не служат источником
его утверждения. Подтверждение согласования находится в диалоге задачи,
не в истории исходного репозитория.
