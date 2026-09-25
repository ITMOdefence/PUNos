## Описание

Syscall Wrappers — минимальный системный ABI нативных компонентов PUNos для
Linux x86_64. Слой предоставляет C-совместимые функции над инструкцией
`syscall`, преобразует ошибки ядра в `errno` и скрывает различия между
публичной функцией PUNos и фактическим syscall Linux.

Wrappers не используют glibc, системные заголовки host-системы, System Libs,
allocator, IPC или логирование. Все типы, структуры, константы и номера syscall
определяются заголовками PUNos и проверяются против UAPI зафиксированного ядра
Linux 6.12.111 для x86_64.

## Граница совместимости

- Поддерживается только Linux x86_64.
- Публичный ABI wrappers принадлежит PUNos и не обещает бинарную совместимость
  с glibc.
- Имена функций похожи на POSIX и libc, но их точное поведение определяется
  этим документом.
- Номера syscall фиксируются для x86_64 и не переносятся на другие архитектуры.
- Wrappers не обращаются к vDSO: каждый вызов из этой спецификации выполняется
  инструкцией `syscall`.
- Добавление нового wrapper требует указать фактический syscall, его номер,
  ABI аргументов, преобразование результата и потребляющий компонент.

## ABI инструкции `syscall`

Перед выполнением инструкции регистры заполняются следующим образом:

| Значение | Регистр |
| --- | --- |
| Номер syscall | `rax` |
| Аргумент 1 | `rdi` |
| Аргумент 2 | `rsi` |
| Аргумент 3 | `rdx` |
| Аргумент 4 | `r10` |
| Аргумент 5 | `r8` |
| Аргумент 6 | `r9` |
| Результат | `rax` |

Инструкция `syscall` уничтожает `rcx` и `r11`. Реализация wrapper также
объявляет изменёнными condition codes и память, если syscall читает или
изменяет память по переданному указателю. Остальные callee-saved регистры
сохраняются согласно x86_64 System V ABI.

## Ошибки и `errno`

Ядро возвращает ошибки как значения от `-4095` до `-1` в `rax`. Публичный
wrapper преобразует такое значение следующим образом:

1. Записывает положительный код `-result` в `errno` текущего потока.
2. Возвращает `-1`, если функция имеет целочисленный результат.
3. Возвращает `MAP_FAILED`, если функцией является `mmap()`.

Успешный вызов не изменяет `errno`. Wrapper не повторяет syscall автоматически
после `EINTR`, не дописывает частичный `write()` и не дочитывает частичный
`read()`. Такая политика принадлежит библиотеке более высокого уровня.

`errno` имеет тип `i32` и предоставляется как:

```c
i32 *sys_errno_location(void);
#define errno (*sys_errno_location())
```

Указатель относится только к текущему потоку. CRT создаёт начальное
thread-local состояние до вызова `main()`, а `libthread` создаёт отдельное
состояние для каждого нового потока и передаёт его через `CLONE_SETTLS`.
Публичные wrappers, способные установить `errno`, вызываются только после
инициализации thread-local состояния.

Низкоуровневая raw-функция, необходимая CRT для первоначальной настройки TLS,
является внутренней деталью реализации и не входит в публичный API wrappers.
Она возвращает значение ядра без преобразования и без обращения к `errno`.
На Linux x86_64 она вызывает `arch_prctl` (`158`) с операцией
`ARCH_SET_FS = 0x1002` и адресом начального `RuntimeThreadState`.

## Типы и структуры ABI

| Наименование | Представление | Назначение |
| --- | --- | --- |
| `size_t` | `u64` | Размер объекта или буфера |
| `ssize_t` | `i64` | Размер результата с `-1` для ошибки wrapper |
| `off_t` | `i64` | Смещение файла |
| `pid_t` | `i32` | PID или TID |
| `mode_t` | `u32` | Режим и права файла |
| `socklen_t` | `u32` | Размер socket-адреса |
| `time_t` | `i64` | Число секунд |
| `clockid_t` | `i32` | Идентификатор часов |
| `struct timespec` | `time_t tv_sec; i64 tv_nsec;` | Интервал или момент времени |
| `struct itimerspec` | `struct timespec it_interval; struct timespec it_value;` | Параметры timerfd |
| `sigset_t` | `u64` | Маска сигналов Linux x86_64; размер для `rt_sig*` равен `8` |
| `sighandler_t` | `void (*)(int signum)` | Обработчик обычного сигнала |
| `struct sigaction` | `sighandler_t sa_handler; u64 sa_flags; void (*sa_restorer)(void); sigset_t sa_mask;` | Kernel ABI действия сигнала x86_64 |
| `struct stat` | `u64 st_dev; u64 st_ino; u64 st_nlink; u32 st_mode; u32 st_uid; u32 st_gid; u32 __pad0; u64 st_rdev; i64 st_size; i64 st_blksize; i64 st_blocks; u64 st_atime; u64 st_atime_nsec; u64 st_mtime; u64 st_mtime_nsec; u64 st_ctime; u64 st_ctime_nsec; i64 __unused[3];` | Kernel ABI результата `fstat()` x86_64 |
| `struct sockaddr` | `u16 sa_family; char sa_data[14];` | Базовый socket-адрес |
| `struct sockaddr_un` | `u16 sun_family; char sun_path[108];` | Unix domain socket-адрес |
| `struct in_addr` | `u32 s_addr;` | IPv4-адрес в сетевом порядке байтов |
| `struct sockaddr_in` | `u16 sin_family; u16 sin_port; struct in_addr sin_addr; u8 sin_zero[8];` | IPv4 socket-адрес |
| `struct in6_addr` | `u8 s6_addr[16];` | IPv6-адрес |
| `struct sockaddr_in6` | `u16 sin6_family; u16 sin6_port; u32 sin6_flowinfo; struct in6_addr sin6_addr; u32 sin6_scope_id;` | IPv6 socket-адрес |
| `epoll_data_t` | Объединение `void *ptr`, `i32 fd`, `u32 u32`, `u64 u64` | Пользовательские данные epoll |
| `struct epoll_event` | Упакованная структура `u32 events; epoll_data_t data;` | Kernel ABI события epoll x86_64 |
| `clone_entry_t` | `i32 (*)(void *argument)` | Начальная функция нового потока |

Размеры и смещения ABI-структур проверяются compile-time assertions. Структуры
нельзя расширять, переупорядочивать или выравнивать по правилам host libc.

## Константы ABI

| Группа | Константы |
| --- | --- |
| Файловые дескрипторы | `STDIN_FILENO = 0`, `STDOUT_FILENO = 1`, `STDERR_FILENO = 2` |
| Открытие файлов | `O_RDONLY = 0`, `O_WRONLY = 1`, `O_RDWR = 2`, `O_CREAT = 64`, `O_TRUNC = 512`, `O_APPEND = 1024`, `O_NONBLOCK = 2048`, `O_CLOEXEC = 524288` |
| Позиция в файле | `SEEK_SET = 0`, `SEEK_CUR = 1`, `SEEK_END = 2` |
| Тип файла | `S_IFMT = 0170000`, `S_IFREG = 0100000`, `S_IFDIR = 0040000`, `S_IFCHR = 0020000`, `S_IFBLK = 0060000` |
| Виртуальная память | `PROT_NONE = 0`, `PROT_READ = 1`, `PROT_WRITE = 2`, `PROT_EXEC = 4`, `MAP_SHARED = 1`, `MAP_PRIVATE = 2`, `MAP_ANONYMOUS = 32`, `MAP_FAILED = (void *)-1` |
| Ожидание процесса | `WNOHANG = 1` |
| Сигналы | `SIG_DFL = (sighandler_t)0`, `SIG_IGN = (sighandler_t)1`, `SIGINT = 2`, `SIGKILL = 9`, `SIGPIPE = 13`, `SIGTERM = 15`, `SIGCHLD = 17`, `SIG_BLOCK = 0`, `SIG_UNBLOCK = 1`, `SIG_SETMASK = 2`, `SA_RESTORER = 0x04000000`, `SA_RESTART = 0x10000000` |
| Часы и таймеры | `CLOCK_REALTIME = 0`, `CLOCK_MONOTONIC = 1`, `TFD_NONBLOCK = 2048`, `TFD_CLOEXEC = 524288`, `TFD_TIMER_ABSTIME = 1` |
| Потоки | `CLONE_VM = 0x00000100`, `CLONE_FS = 0x00000200`, `CLONE_FILES = 0x00000400`, `CLONE_SIGHAND = 0x00000800`, `CLONE_THREAD = 0x00010000`, `CLONE_SYSVSEM = 0x00040000`, `CLONE_SETTLS = 0x00080000`, `CLONE_PARENT_SETTID = 0x00100000`, `CLONE_CHILD_CLEARTID = 0x00200000`, `CLONE_CHILD_SETTID = 0x01000000` |
| Futex | `FUTEX_WAIT = 0`, `FUTEX_WAKE = 1`, `FUTEX_PRIVATE_FLAG = 128`, `FUTEX_WAIT_PRIVATE = 128`, `FUTEX_WAKE_PRIVATE = 129` |
| Сокеты | `AF_UNIX = 1`, `AF_INET = 2`, `AF_INET6 = 10`, `SOCK_STREAM = 1`, `SOCK_NONBLOCK = 2048`, `SOCK_CLOEXEC = 524288`, `SOL_SOCKET = 1`, `SO_REUSEADDR = 2`, `SO_ERROR = 4`, `SHUT_RD = 0`, `SHUT_WR = 1`, `SHUT_RDWR = 2`, `MSG_NOSIGNAL = 16384` |
| Epoll и eventfd | `EPOLLIN = 1`, `EPOLLOUT = 4`, `EPOLLERR = 8`, `EPOLLHUP = 16`, `EPOLL_CTL_ADD = 1`, `EPOLL_CTL_DEL = 2`, `EPOLL_CTL_MOD = 3`, `EPOLL_CLOEXEC = 524288`, `EFD_NONBLOCK = 2048`, `EFD_CLOEXEC = 524288` |
| Mount | `MS_RDONLY = 1`, `MS_NOSUID = 2`, `MS_NODEV = 4`, `MS_NOEXEC = 8`, `MS_SYNCHRONOUS = 16`, `MS_REMOUNT = 32`, `MS_BIND = 4096`, `MS_REC = 16384`, `MS_PRIVATE = 262144` |
| Ошибки | `EPERM = 1`, `ENOENT = 2`, `ESRCH = 3`, `EINTR = 4`, `EIO = 5`, `EBADF = 9`, `ECHILD = 10`, `EAGAIN = 11`, `ENOMEM = 12`, `EACCES = 13`, `EFAULT = 14`, `EBUSY = 16`, `EEXIST = 17`, `ENODEV = 19`, `ENOTDIR = 20`, `EISDIR = 21`, `EINVAL = 22`, `ENOTTY = 25`, `ENOSPC = 28`, `EPIPE = 32`, `ENOSYS = 38`, `ENOTEMPTY = 39`, `EOVERFLOW = 75`, `ENOTSOCK = 88`, `EOPNOTSUPP = 95`, `EADDRINUSE = 98`, `ETIMEDOUT = 110`, `ECONNREFUSED = 111`, `EINPROGRESS = 115` |

`EWOULDBLOCK` является псевдонимом `EAGAIN` и имеет значение `11`.

## Процессы

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат и уточнение |
| --- | --- | --- | --- |
| `getpid()` | Нет | `getpid` (`39`) | PID текущего процесса |
| `fork()` | Нет | `fork` (`57`) | PID ребёнка в родителе, `0` в ребёнке, `-1` при ошибке |
| `execve()` | `const char *path`, `char *const argv[]`, `char *const envp[]` | `execve` (`59`) | Не возвращается при успехе; `-1` при ошибке |
| `waitpid()` | `pid_t pid`, `i32 *status`, `i32 options` | `wait4` (`61`) | Передаёт четвёртым аргументом `NULL`; поддерживает `WNOHANG` |
| `exit()` | `i32 status` | `exit_group` (`231`) | Завершает весь процесс со всеми потоками; не возвращается |
| `kill()` | `pid_t pid`, `i32 signal` | `kill` (`62`) | `0` при успехе, `-1` при ошибке |

`fork()` используется только для запуска нового ELF. В дочернем процессе после
fork допустимы только подготовка аргументов без выделения памяти, `execve()` и
`exit()`.

## Виртуальная память

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат |
| --- | --- | --- | --- |
| `mmap()` | `void *address`, `size_t length`, `i32 protection`, `i32 flags`, `i32 fd`, `off_t offset` | `mmap` (`9`) | Адрес отображения или `MAP_FAILED` |
| `mprotect()` | `void *address`, `size_t length`, `i32 protection` | `mprotect` (`10`) | `0` или `-1` |
| `munmap()` | `void *address`, `size_t length` | `munmap` (`11`) | `0` или `-1` |

## Сигналы

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Уточнение |
| --- | --- | --- | --- |
| `sigaction()` | `i32 signal`, `const struct sigaction *action`, `struct sigaction *old_action` | `rt_sigaction` (`13`) | Четвёртый аргумент syscall всегда равен `8` |
| `sigprocmask()` | `i32 how`, `const sigset_t *set`, `sigset_t *old_set` | `rt_sigprocmask` (`14`) | Четвёртый аргумент syscall всегда равен `8` |

При установке пользовательского обработчика `sigaction()` создаёт локальную
копию структуры, устанавливает `SA_RESTORER` и записывает адрес внутреннего
assembly-trampoline `__restore_rt` в `sa_restorer`. Trampoline выполняет
`rt_sigreturn` (`15`). Вызывающий код не предоставляет собственный restorer.

## Файлы и каталоги

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат |
| --- | --- | --- | --- |
| `open()` | `const char *path`, `i32 flags`, `mode_t mode` | `open` (`2`) | Новый fd или `-1` |
| `close()` | `i32 fd` | `close` (`3`) | `0` или `-1` |
| `read()` | `i32 fd`, `void *buffer`, `size_t count` | `read` (`0`) | Число байтов, `0` для EOF или `-1` |
| `write()` | `i32 fd`, `const void *buffer`, `size_t count` | `write` (`1`) | Число байтов или `-1` |
| `lseek()` | `i32 fd`, `off_t offset`, `i32 origin` | `lseek` (`8`) | Новая позиция или `-1` |
| `fstat()` | `i32 fd`, `struct stat *info` | `fstat` (`5`) | `0` или `-1` |
| `mkdir()` | `const char *path`, `mode_t mode` | `mkdir` (`83`) | `0` или `-1` |
| `unlink()` | `const char *path` | `unlink` (`87`) | `0` или `-1` |
| `rename()` | `const char *old_path`, `const char *new_path` | `rename` (`82`) | `0` или `-1` |
| `fsync()` | `i32 fd` | `fsync` (`74`) | `0` или `-1` |

`open()`, `mkdir()`, `unlink()` и `rename()` намеренно используют доступные в
x86_64 legacy syscalls. Переход на семейство `*at` является отдельным
изменением публичного контракта.

## Файловые системы ранней загрузки

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат |
| --- | --- | --- | --- |
| `mount()` | `const char *source`, `const char *target`, `const char *filesystem`, `u64 flags`, `const void *data` | `mount` (`165`) | `0` или `-1` |

`source` может быть `NULL` для файловых систем, которым не требуется блочное
устройство. Wrapper предназначен прежде всего для `/init`; политика выбора
точек, типов и флагов монтирования находится в контракте начального процесса.

`umount2()` и `reboot()` не входят в v1. Они добавляются вместе с контрактом
штатного выключения, а не заранее.

## Время и таймеры

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат |
| --- | --- | --- | --- |
| `clock_gettime()` | `clockid_t clock`, `struct timespec *time` | `clock_gettime` (`228`) | `0` или `-1` |
| `nanosleep()` | `const struct timespec *requested`, `struct timespec *remaining` | `nanosleep` (`35`) | `0` или `-1`; при `EINTR` remaining содержит остаток |
| `timerfd_create()` | `clockid_t clock`, `i32 flags` | `timerfd_create` (`283`) | Новый fd или `-1` |
| `timerfd_settime()` | `i32 fd`, `i32 flags`, `const struct itimerspec *new_value`, `struct itimerspec *old_value` | `timerfd_settime` (`286`) | `0` или `-1` |

## Потоки и синхронизация

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат и уточнение |
| --- | --- | --- | --- |
| `clone()` | `clone_entry_t entry`, `void *child_stack`, `u64 flags`, `void *argument`, `i32 *parent_tid`, `void *tls`, `i32 *child_tid` | `clone` (`56`) | TID в родителе; ребёнок начинает выполнение с `entry(argument)` |
| `gettid()` | Нет | `gettid` (`186`) | TID текущего потока |
| `thread_exit()` | `i32 status` | `exit` (`60`) | Завершает только текущий поток; не возвращается |
| `futex()` | `u32 *address`, `i32 operation`, `u32 value`, `const struct timespec *timeout`, `u32 *second_address`, `u32 third_value` | `futex` (`202`) | Результат операции или `-1` |

`clone()` является C-совместимой обёрткой с assembly-trampoline, а не прямой
функцией `syscall; ret`. Реальный порядок аргументов syscall x86_64:

```text
clone(flags, child_stack, parent_tid, tls, child_tid)
```

В ребёнке kernel заменяет `rsp` на `child_stack`, поэтому обычный `ret` из
wrapper недопустим. Trampoline вызывает `entry(argument)` и передаёт её код
возврата в `thread_exit()`. `child_stack` указывает на выровненную по 16 байтам
верхнюю границу отдельного стека, выделенного вызывающей библиотекой.

## Локальные и сетевые сокеты

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Уточнение |
| --- | --- | --- | --- |
| `socket()` | `i32 domain`, `i32 type`, `i32 protocol` | `socket` (`41`) | Возвращает fd или `-1` |
| `bind()` | `i32 fd`, `const struct sockaddr *address`, `socklen_t length` | `bind` (`49`) | `0` или `-1` |
| `listen()` | `i32 fd`, `i32 backlog` | `listen` (`50`) | `0` или `-1` |
| `accept()` | `i32 fd`, `struct sockaddr *address`, `socklen_t *length` | `accept` (`43`) | Возвращает fd или `-1` |
| `accept4()` | `i32 fd`, `struct sockaddr *address`, `socklen_t *length`, `i32 flags` | `accept4` (`288`) | Возвращает fd или `-1` |
| `connect()` | `i32 fd`, `const struct sockaddr *address`, `socklen_t length` | `connect` (`42`) | `0` или `-1` |
| `getsockopt()` | `i32 fd`, `i32 level`, `i32 option`, `void *value`, `socklen_t *length` | `getsockopt` (`55`) | `0` или `-1` |
| `setsockopt()` | `i32 fd`, `i32 level`, `i32 option`, `const void *value`, `socklen_t length` | `setsockopt` (`54`) | `0` или `-1` |
| `send()` | `i32 fd`, `const void *buffer`, `size_t length`, `i32 flags` | `sendto` (`44`) | Передаёт `NULL, 0` как адрес назначения |
| `recv()` | `i32 fd`, `void *buffer`, `size_t length`, `i32 flags` | `recvfrom` (`45`) | Передаёт `NULL, NULL` как адрес отправителя |
| `shutdown()` | `i32 fd`, `i32 how` | `shutdown` (`48`) | `0` или `-1` |

Один набор wrappers используется для `AF_UNIX`, `AF_INET` и `AF_INET6`.
Ограничения поддерживаемых протоколов определяются библиотекой-потребителем.

## Ожидание событий

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат |
| --- | --- | --- | --- |
| `epoll_create1()` | `i32 flags` | `epoll_create1` (`291`) | Новый fd или `-1` |
| `epoll_ctl()` | `i32 epoll_fd`, `i32 operation`, `i32 fd`, `struct epoll_event *event` | `epoll_ctl` (`233`) | `0` или `-1` |
| `epoll_wait()` | `i32 epoll_fd`, `struct epoll_event *events`, `i32 max_events`, `i32 timeout_ms` | `epoll_wait` (`232`) | Число событий, `0` для timeout или `-1` |
| `eventfd()` | `u32 initial_value`, `i32 flags` | `eventfd2` (`290`) | Новый fd или `-1` |

Публичный `eventfd()` использует `eventfd2`, потому что контракт принимает
flags. Legacy syscall `eventfd` (`284`) не используется.

## Устройства

| Wrapper | Публичные аргументы | Реальный syscall x86_64 | Результат |
| --- | --- | --- | --- |
| `ioctl()` | `i32 fd`, `u64 request`, `unsigned long argument` | `ioctl` (`16`) | Значение команды или `-1` |

Третий аргумент представлен целым машинного размера, поскольку разные команды
ioctl принимают указатель, целое значение или не используют аргумент. Wrapper
не интерпретирует request и argument.

## Итоги аудита Linux 6.12.111 x86_64

В результате аудита исходная спецификация уточнена следующим образом:

1. `waitpid()` отображён на `wait4` с `rusage = NULL`.
2. `exit()` отображён на `exit_group`, а `thread_exit()` — на `exit`.
3. `sigaction()` и `sigprocmask()` отображены на семейство `rt_sig*` с размером
   маски `8`.
4. Для обработчиков сигналов определён внутренний `rt_sigreturn` trampoline.
5. `send()` и `recv()` отображены на `sendto` и `recvfrom` с пустыми адресами.
6. `eventfd()` отображён на `eventfd2`, поскольку принимает flags.
7. Для `clone()` определён безопасный C-совместимый trampoline и реальный
   порядок аргументов x86_64.
8. `ioctl()` принимает машинное целое, а не только указатель.
9. Добавлен `mount()` для начального процесса `/init`.
10. Зафиксированы регистры, правила ошибок, TLS-контракт `errno` и отсутствие
    автоматического повторения после `EINTR`.
