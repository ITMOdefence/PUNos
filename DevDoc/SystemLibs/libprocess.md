## Описание

Системная библиотека для запуска, ожидания и остановки нативных компонентов
платформы: VM и системных сервисов.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libprocess`. Готовы:

- публичный заголовок `<sys/process.h>` со всеми типами и константами;
- статическая библиотека `libpunos_process.a` со всеми операциями из таблицы
  ниже;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест (`tests/process_runtime.c`), который запускает сам себя как
  дочерний процесс и проверяет коды выхода, сигналы, argv и окружение, poll,
  terminate и прерванное ожидание. Выполняется на хосте командой
  `make -C System/SystemLibs/libprocess check` и внутри PUNos в QEMU как
  `/punos/bin/libprocess-selftest`.

Библиотека сама вызывает только `getpid()`, `fork()`, `execve()`,
`waitpid()`, `exit()` и `kill()` (контракт 1); память для handle и массивов
argv/envp выделяется через `libmem`. Решения, принятые при реализации,
описаны в `System/SystemLibs/libprocess/README.md`.

## Имя библиотеки
`sys/process`

Заголовок — `<sys/process.h>`, как в `_LibsList.md` и спецификациях
потребителей.
## Контракты

1. Библиотека использует только Syscall Wrappers `getpid()`, `fork()`,
   `execve()`, `waitpid()`, `exit()` и `kill()`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. `process_start()` является низкоуровневой операцией для `initserv` и
   системных сервисов; bytecode-приложения не получают к ней доступ через
   состав поставки и загрузчик платформы.
4. `process_start()` не проверяет регистрацию executable в PlatformRegistry:
   в её API нет реестра или capability, поэтому такая проверка не может быть
   достоверно реализована в библиотеке. Политику разрешённого запуска до вызова
   операции проверяет владелец каталога — `initserv` для System Services и
   `appserv` для пользовательских приложений.
5. После `fork()` дочерний процесс вызывает только `execve()` или `exit()`.
6. `ProcessHandle` созданного процесса должен быть завершён через
   `process_wait()` или `process_poll()` после его остановки.
7. `process_wait()` и `process_poll()` применимы только к дочерним процессам
   текущего процесса.
8. `process_terminate()` отправляет процессу `SIGTERM`; она не ожидает его
   фактического завершения.
9. `ProcessLaunch` и строки, на которые он указывает, не переходят во владение
   библиотеки и должны быть действительны только на время `process_start()`.
10. `argv` дочернего процесса — `executable`, затем `arguments[0 …
    argument_count)`; окружение — ровно `environment[0 … environment_count)`
    в форме `ИМЯ=значение`, ничего не наследуется неявно. Массив может быть
    `NULL`, если его длина `0`.
11. Всё, что может завершиться ошибкой (массивы argv/envp, handle), готовится
    до `fork()`. Если `execve()` в дочернем процессе не удался, он завершается
    с кодом `PROCESS_EXIT_EXEC_FAILED` (`127`); `process_start()` в этом случае
    уже вернул `CORE_OK`, и ошибка видна через `process_wait()`.
12. Если `process_wait()` прерван обработчиком сигнала, он возвращает
    `CORE_INTERRUPTED` и сохраняет handle. `process_wait()` и `process_poll()`
    освобождают handle и записывают `NULL` только после получения статуса;
    при ошибке handle остаётся действительным.
13. Нулевые указатели и строки, не являющиеся действительными `String`, дают
    `CORE_INVALID_ARGUMENT`; слишком большие длины массивов — `CORE_OVERFLOW`.

## Типы

| Наименование             | Представление                                                                                                                 | Допустимые значения                           | Назначение                                          |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- | --------------------------------------------------- |
| `String`                 | Тип из `libstr`                                                                                                               | UTF-8 строка без `NUL`                        | Путь к компоненту, аргументы и переменные окружения |
| `ProcessId`              | `i32` из `libcore`                                                                                                            | Положительный идентификатор процесса          | Идентификатор нативного процесса                    |
| `ProcessHandle`          | Непрозрачный указатель: `struct ProcessHandle *`                                                                              | `NULL` или только запущенный дочерний процесс | Handle дочернего процесса                           |
| `ProcessLaunch`          | Структура: `const String *executable`, `const String *arguments`, `usize argument_count`, `const String *environment`, `usize environment_count` | Только зарегистрированный компонент платформы | Параметры запуска процесса                          |
| `ProcessTerminationKind` | Перечисление                                                                                                                  | `Exited` (`0`), `Signaled` (`1`)              | Причина завершения процесса; в C — `PROCESS_TERMINATION_KIND_EXITED`, `PROCESS_TERMINATION_KIND_SIGNALED` |
| `ProcessExitStatus`      | Структура: `ProcessTerminationKind kind`, `i32 value`                                                                         | Код выхода или номер сигнала                  | Результат завершения дочернего процесса             |

## Константы

| Наименование           | Назначение                             |
| ---------------------- | -------------------------------------- |
| `PROCESS_EXIT_SUCCESS` | Код успешного завершения процесса: `0` |
| `PROCESS_EXIT_FAILURE` | Код общей ошибки процесса: `1` |
| `PROCESS_EXIT_INVALID_ARGUMENT` | Код завершения при недопустимых аргументах запуска: `2` |
| `PROCESS_EXIT_INITIALIZATION_FAILED` | Код завершения при ошибке инициализации компонента: `3` |
| `PROCESS_EXIT_EXEC_FAILED` | Код завершения дочернего процесса, если `execve()` не удался: `127` |

## Операции

| Операция                                          | Аргументы                                                                       | Возвращаемое значение | Назначение                                                                     |
| ------------------------------------------------- | ------------------------------------------------------------------------------- | --------------------- | ------------------------------------------------------------------------------ |
| `process_current_id(out_pid)`                     | `ProcessId *out_pid`                                                            | `ErrorCode`           | Получает идентификатор текущего процесса через `getpid()`                      |
| `process_start(launch, out_process)`              | `const ProcessLaunch *launch`, `ProcessHandle *out_process`                     | `ErrorCode`           | Создаёт дочерний процесс через `fork()` и запускает компонент через `execve()` |
| `process_poll(process, out_finished, out_status)` | `ProcessHandle *process`, `bool *out_finished`, `ProcessExitStatus *out_status` | `ErrorCode`           | Проверяет через `waitpid()` без блокировки; при завершении освобождает handle и записывает `NULL` |
| `process_wait(process, out_status)`               | `ProcessHandle *process`, `ProcessExitStatus *out_status`                       | `ErrorCode`           | Ожидает завершения дочернего процесса через `waitpid()`                        |
| `process_terminate(process)`                      | `ProcessHandle process`                                                         | `ErrorCode`           | Отправляет дочернему процессу `SIGTERM` через `kill()`                         |
