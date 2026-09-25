## Описание

Системная библиотека для создания нативных потоков, ожидания их завершения и
получения результата работы.
## Имя библиотеки
`sys/thread`
## Контракты

1. Библиотека использует Syscall Wrappers `clone()`, `gettid()` и `futex()`,
   внутренний ABI `RuntimeThreadState` из `<runtime/thread-state.h>`, а также
   `libcore` и `libmem`. `thread_exit()` вызывается внутренним
   assembly-trampoline самого `clone()`, а не кодом `libthread`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. `ThreadEntry` вызывается ровно один раз в созданном потоке.
4. Аргумент `ThreadEntry` не переходит во владение библиотеки и должен быть
   действителен, пока поток не завершён.
5. `ThreadHandle` должен быть передан в `thread_join()` ровно один раз.
6. `thread_join()` нельзя вызывать из потока, соответствующего переданному
   `ThreadHandle`.
7. `thread_join()` ожидает завершение потока, освобождает его внутренний стек и
   делает `ThreadHandle` недействительным.
8. Принудительное завершение потока не поддерживается. Поток завершает работу,
   когда его `ThreadEntry` возвращает значение.
9. Внутренний C-trampoline `libthread` сохраняет код возврата `ThreadEntry` в
   `ThreadHandle` и возвращает этот код assembly-trampoline `clone()`.
   Assembly-trampoline вызывает `thread_exit()` после возврата C-trampoline.
10. Для каждого потока библиотека выделяет отдельные стек и
    `RuntimeThreadState`. Поле self в RuntimeThreadState указывает на саму
    структуру, а errno_value при создании равен `0`.
11. `thread_create()` передаёт RuntimeThreadState как TLS base в `clone()` с
    флагом `CLONE_SETTLS`. Поток не разделяет `errno` с создавшим потоком.
12. Стек нового потока передаётся в `clone()` как верхняя граница, выровненная
    по 16 байтам. В качестве clone entry используется внутренний trampoline
    `libthread`, которому передаётся контекст ThreadHandle.
13. RuntimeThreadState и стек остаются во владении ThreadHandle до успешного
    `thread_join()` и освобождаются только после завершения потока.
14. `RuntimeThreadState` подключается из внутреннего заголовка
    `<runtime/thread-state.h>`. `libthread` не объявляет собственную копию
    структуры и не меняет смещения её полей.
15. `thread_create()` передаёт в `clone()` флаги `CLONE_VM`, `CLONE_FS`,
    `CLONE_FILES`, `CLONE_SIGHAND`, `CLONE_THREAD`, `CLONE_SYSVSEM`,
    `CLONE_SETTLS`, `CLONE_PARENT_SETTID`, `CLONE_CHILD_SETTID` и
    `CLONE_CHILD_CLEARTID`. Одно поле TID внутри `ThreadHandle` передаётся как
    `parent_tid` и `child_tid`.
16. `thread_join()` циклически читает поле TID и, пока оно ненулевое, ожидает
    его через `futex(FUTEX_WAIT)`. `EINTR` и изменение значения до входа в
    ядро не означают ошибку join: условие проверяется повторно.
17. Ядро обнуляет поле TID и пробуждает futex при завершении потока благодаря
    `CLONE_CHILD_CLEARTID`. Только наблюдение нулевого TID разрешает
    `thread_join()` прочитать сохранённый результат и освободить стек,
    `RuntimeThreadState` и `ThreadHandle`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `ThreadId` | `i32` из `libcore` | Положительный идентификатор потока | Идентификатор нативного потока |
| `ThreadEntry` | Указатель на функцию: `i32 (*)(void *argument)` | Действительная функция | Функция, выполняемая новым потоком |
| `ThreadHandle` | Непрозрачный указатель: `struct ThreadHandle *` | `NULL` или только созданный и не присоединённый поток | Handle, владеющий стеком, RuntimeThreadState, результатом и состоянием завершения потока |
| `ThreadExitStatus` | `i32` из `libcore` | Код, возвращённый `ThreadEntry` | Результат работы потока |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `THREAD_EXIT_SUCCESS` | Успешный код завершения потока: `0` |
| `THREAD_EXIT_FAILURE` | Общий код ошибки потока: `1` |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `thread_current_id(out_thread_id)` | `ThreadId *out_thread_id` | `ErrorCode` | Получает идентификатор текущего потока через `gettid()` |
| `thread_create(entry, argument, out_thread)` | `ThreadEntry entry`, `void *argument`, `ThreadHandle *out_thread` | `ErrorCode` | Выделяет стек и RuntimeThreadState через `libmem`, создаёт поток через `clone()` и запускает внутренний trampoline, который вызывает `entry` |
| `thread_join(thread, out_status)` | `ThreadHandle *thread`, `ThreadExitStatus *out_status` | `ErrorCode` | Ожидает обнуления TID ядром через `futex()`, возвращает код `ThreadEntry`, освобождает ресурсы и записывает `NULL` |
