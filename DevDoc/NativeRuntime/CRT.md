CRT — внутренний стартовый runtime нативных компонентов. Он используется
`/init`, VM, `initserv`, системными сервисами и нативными приложениями.
Bytecode-приложения CRT не используют: их запускает VM.

## Общий контракт

Для каждого нативного исполняемого файла CRT передаёт управление функции:

```c
int main(int argc, char **argv, char **envp);
```

Функция `main()` реализуется самим компонентом. Она возвращает код
завершения процесса.

CRT завершает процесс через функцию из слоя Syscall Wrappers:

```c
_Noreturn void exit(int status);
```

До передачи управления в `main()` CRT инициализирует thread-local состояние
главного потока, необходимое Syscall Wrappers для хранения `errno`.

## Thread-local состояние runtime

Каждый нативный поток имеет внутренний `RuntimeThreadState` со стабильным ABI:

```c
struct RuntimeThreadState {
    struct RuntimeThreadState *self;
    i32 errno_value;
    i32 reserved;
};
```

Поле `self` находится по смещению `0` и указывает на начало той же структуры.
FS base потока указывает на `RuntimeThreadState`. `sys_errno_location()` читает
`self` через `%fs:0` и возвращает адрес `errno_value`.

CRT владеет статическим `RuntimeThreadState` главного потока. `libthread`
владеет состояниями создаваемых им потоков. Структура объявляется во внутреннем
заголовке `<runtime/thread-state.h>`. Она не является публичным API приложений
и используется только CRT, Syscall Wrappers и `libthread`.

## `crt0.S`

`crt0.S` — архитектурно-зависимый файл для Linux x86_64. Он экспортирует
единственную точку входа ELF:

```text
_start
```

### Входные данные

При входе Linux размещает на вершине стека следующие данные:

- `argc` — число аргументов;
- `argv` — массив указателей на аргументы, завершающийся `NULL`;
- `envp` — массив указателей на переменные окружения, завершающийся `NULL`.

### Обязательства

- Извлечь `argc`, `argv` и `envp` со стартового стека.
- Выровнять стек для вызова функции по x86_64 System V ABI.
- Инициализировать статический `RuntimeThreadState` главного потока.
- Установить FS base через внутренний raw syscall `arch_prctl(ARCH_SET_FS)` до
  первого публичного Syscall Wrapper.
- Передать извлечённые значения в `crt_start()`.
- Не возвращать управление через инструкцию `ret`.

### Используемый контракт

```c
_Noreturn void crt_start(int argc, char **argv, char **envp);
```

`crt0.S` не использует libc, System Libs, allocator, логирование или файловый
ввод-вывод. Для первоначальной настройки FS ему разрешён только внутренний raw
syscall, который не обращается к `errno`. При ошибке настройки TLS `crt0.S`
завершает процесс внутренним raw `exit_group` с кодом
`CRT_EXIT_TLS_INITIALIZATION_FAILED = 127`.

## `crt.c`

`crt.c` — архитектурно-независимая часть CRT.

### Экспортируемый контракт

```c
_Noreturn void crt_start(int argc, char **argv, char **envp);
```

### Обязательства

- Принять `argc`, `argv` и `envp` от `crt0.S` без изменения их содержимого.
- Считать thread-local состояние уже инициализированным `crt0.S`.
- Вызвать `main(argc, argv, envp)`.
- Передать код, возвращённый `main()`, в `exit(status)`.
- Не возвращать управление в `crt0.S`.

### Ограничения

- CRT не использует libc.
- CRT и нативные компоненты собираются без stack protector и не используют
  compiler-managed TLS; FS принадлежит `RuntimeThreadState` PUNos.
- CRT не инициализирует вручную секцию `.bss`.
- CRT не создаёт процессы, потоки, окна, IPC-соединения и не запускает
  bytecode-приложения.
