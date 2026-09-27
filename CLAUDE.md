# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Обзор проекта

PUNos — нативная userspace-платформа поверх **неизменённого upstream-ядра Linux** (зафиксирована версия 6.12.111 LTS, x86_64), запускаемая в QEMU. glibc не используется: каждый нативный компонент — freestanding статический ELF, собранный на собственных CRT и syscall-слое PUNos.

- `System/` — код и инструменты сборки. Полное руководство: `System/start-guide.md`.
- `DevDoc/` — Obsidian-хранилище со спецификациями всех слоёв, включая ещё не реализованные. Спецификации являются контрактом, реализация обязана им соответствовать (например, `DevDoc/NativeRuntime/Syscall Wrappers.md`, `DevDoc/Boot/init.md`). Общая схема слоёв — `DevDoc/PUNos_architecture.drawio` / `architecture.png`.

Документация, спецификации и руководства пишутся на русском; код, комментарии и README компонентов — на английском.

## Команды

Все сценарии запускаются из корня репозитория и **никогда через `sudo`** (иначе сгенерированные каталоги будут принадлежать root и сломают Git и сборку).

```bash
./System/scripts/prepare-kernel.sh      # скачать, проверить SHA256 и распаковать ядро в System/linux-6.12.111
./System/scripts/build-kernel.sh        # out-of-tree сборка в System/linux-build (JOBS=N ограничивает параллелизм)
./System/scripts/build-initramfs.sh     # собирает Boot/ и NativeRuntime, наполняет System/rootfs, пишет System/images/punos-initramfs.cpio.gz
./System/scripts/run-qemu-initramfs.sh  # обычная загрузка (serial-консоль; выход — Ctrl+A, X)
./System/scripts/run-qemu.sh            # диагностика голого ядра; завершается ожидаемой паникой "Unable to mount root fs"
```

Тесты (freestanding-бинарники запускаются на хосте, QEMU не нужен):

```bash
make -C System/NativeRuntime/SyscallWrappers check   # ABI compile-проверки + raw/public/full runtime-тесты
make -C System/NativeRuntime/CRT check               # minimal_program обязан завершиться с кодом 37
make -C System/SystemLibs check                      # все System Libs; одна библиотека: make -C System/SystemLibs/libcore check
```

`build-initramfs.sh` на машине с русской локалью запускать как `LC_ALL=C ./System/scripts/build-initramfs.sh`: `verify_elf` разбирает вывод `readelf`, который переводится. Скрипт под локаль не править.

Отдельный тест SyscallWrappers запускается сборкой и выполнением его цели, например: `make -C System/NativeRuntime/SyscallWrappers build/public_wrappers_runtime && System/NativeRuntime/SyscallWrappers/build/public_wrappers_runtime`.

Успешная загрузка в QEMU выводит по порядку: `PUNos init: userspace started`, `filesystems ready`, `initserv started`, сообщение initserv stub, затем для каждого self-test строку самого теста и `PUNos initserv stub: <name> self-test passed` (сейчас `syscall` и все 12 реализованных System Libs).

Сборка initramfs обязана быть воспроизводимой: два последовательных запуска `build-initramfs.sh` должны давать одинаковый `sha256sum`. `build-initramfs.sh` также отклоняет любой ELF с сегментом `PT_INTERP` или неразрешёнными символами.

## Архитектура

Слои (снизу вверх; большинство верхних слоёв пока существуют только как спецификации в `DevDoc/`). UI-слой (`DevDoc/SystemServices/UI/`, `displaylib`, `uilib`, ветка `GUI`) не трогать — им занимается не эта работа.

1. **Ядро** — в `System/kernel/` хранятся только зафиксированные `version`, `SHA256SUMS` и эталонный `configs/x86_64.config`. Исходники и результаты сборки генерируются и игнорируются Git. Смена версии ядра требует обновления всех трёх файлов.
2. **NativeRuntime** (`System/NativeRuntime/`)
   - `SyscallWrappers` → `libpunos_syscall.a`. Публичный API — `#include <punos/syscall/syscall.h>`; `types.h` — **единственный источник** машинных и Linux ABI-типов (`u8`–`u64`, `i8`–`i64`, `pid_t`, …) для всего userspace PUNos (libcore реэкспортирует их, а не объявляет заново). Raw-вызовы (`__punos_syscall0..6`), номера syscall (`src/x86_64/syscall_numbers.h`) и преобразование результата находятся в `src/internal`/`src/x86_64` и **не** являются публичными — raw-вход может использовать только bootstrap CRT (`arch_prctl`, аварийный `exit_group`). Wrappers всегда используют инструкцию `syscall` (без vDSO). По спецификации добавление wrapper требует описать syscall, его номер, ABI аргументов, преобразование результата и потребляющий компонент.
   - `CRT` → `crt0.o` + `libpunos_crt.a`. `_start` инициализирует `RuntimeThreadState` главного потока и FS base (thread-local `errno`), вызывает `main(argc, argv, envp)` и завершается через wrapper `exit()` PUNos.
   - `include/runtime/thread-state.h` — общий ABI `RuntimeThreadState` (раскладка проверяется static assert). CRT и будущий `libthread` обязаны использовать это объявление, а не локальные копии.
3. **System Libs** (`System/SystemLibs/`) — по каталогу на библиотеку с `include/sys/<name>.h`, `src/`, `tests/abi_compile.c`, `tests/<name>_runtime.c` и архивом `build/libpunos_<name>.a`. Makefile библиотеки — 4 строки (`LIBRARY_NAME`, `RUNTIME_TEST`, `DEPENDENCIES`, `include ../library.mk`); все правила сборки в `System/SystemLibs/library.mk`. Соглашения (цели Makefile, формат тестов, коды выхода по номеру проверки, порядок линковки) — в `System/SystemLibs/README.md`. Реализованы `libcore` (`<sys/core.h>`: базовые типы, `ErrorCode` — числовые значения являются ABI, `core_size_add/multiply`, `core_error_from_errno`), `libmath` (`<sys/math.h>`: чистые функции `f64`→`f64`/`bool`, без `ErrorCode` и без префикса имени библиотеки — так задано в спецификации; `floor/ceil/round` через побитовую работу с IEEE 754, `sqrt` через `sqrtsd`, `sin/cos/atan2` через x87 `FSIN/FCOS/FPATAN`) `libmem` (`<sys/memory.h>`: каждый непустой блок — отдельный `mmap` с округлением до страниц, кучи нет), `libstr` (`<sys/string.h>`: неизменяемые UTF-8 строки, проверка по RFC 3629, каждая непустая строка — минимум страница), `libfile` (`<sys/file.h>`: `FileHandle` не отдаёт fd наружу, нет `rmdir`/`directory_remove` — контракт не разрешает), `libtime` (`<sys/time.h>`: миллисекунды в `u64`, `CLOCK_MONOTONIC`/`CLOCK_REALTIME`), `libprocess` (`<sys/process.h>`: всё готовится до `fork()`, в ребёнке только `execve`/`exit(127)`; тест запускает сам себя через `/proc/self/exe`), `libthread` (`<sys/thread.h>`: handle + `RuntimeThreadState` в одной странице, стек 256 КиБ без guard-страницы, join через `FUTEX_WAIT` без PRIVATE по `CLONE_CHILD_CLEARTID`), `libsync` (`<sys/sync.h>`: mutex/event/condition только на `futex()`, трёхсостояньевый mutex по Дрепперу, condition — счётчик поколений), `libeventloop` (`<sys/eventloop.h>` — заголовок из `_LibsList.md`, не `sys/event` со страницы спеки; epoll+eventfd+timerfd, таблица источников растёт через `libmem`, scratch-буфер `epoll_wait` переиспользуется между вызовами), `libinputdevice` (`<sys/input-device.h>`: Linux evdev ABI написан руками — в PUNos нет `<linux/input.h>`; `EVIOCGID`/`EVIOCGBIT` пересчитаны через формулу `_IOC`, сверены с реальным ядром) и `libv4l2` (`<sys/v4l2.h>`: V4L2 ABI тоже написан руками и сверен построчно с `/usr/include/linux/videodev2.h` (размеры структур критичны — они зашиты в номер ioctl); `VIDIOC_TRY_FMT` вместо `S_FMT` при переборе конфигураций, чтобы не менять текущий формат устройства). У `libinputdevice`/`libv4l2` тесты адаптивные — под root/через sudo реально открывают устройство и гоняют полный success-path, без прав — только error-path (NOT_FOUND/PERMISSION_DENIED); `libv4l2` на текущей машине без `/dev/video*` (нужен `v4l2loopback`, не устанавливался без запроса). Потоковые/сигнальные тесты гонять многократно (хост ×30, QEMU ×5): гонки проявляются не каждый раз. Имя заголовка брать из `_LibsList.md`, а не из раздела «Имя библиотеки» страницы библиотеки (там встречаются сокращения `sys/mem`, `sys/str`). Внутренние функции, нужные тесту, объявляются в `src/internal.h` с префиксом `lib<name>_internal_`. Runtime-тесты используют общий `System/SystemLibs/testing/check.h` (`EXPECT(N, cond)`, N — код выхода 1–255). Значения перечислений в C: `<TYPE_NAME>_<VALUE>` (`MEMORY_COMPARE_RESULT_LESS`). Чек-лист добавления новой библиотеки (Makefile, Boot, `SELF_TESTS` в `build-initramfs.sh`, stub, start-guide, DevDoc) — в `System/SystemLibs/README.md`. Кода `memcpy`/`memset` в PUNos нет: байтовые циклы собирать с `-fno-tree-loop-distribute-patterns`, проверять `nm --undefined-only`.
4. **Boot** (`System/Boot/`) — `/init` (PID 1: проверяет PID, монтирует devtmpfs/proc/sysfs/tmpfs, заново создаёт `/run/punos`, через fork+exec запускает `/punos/bin/initserv`, освобождает дочерние процессы; никогда не возвращается из `main`) и `InitservStub` — smoke-test заглушка на месте `/punos/bin/initserv`, которая по очереди запускает `/punos/bin/*-selftest` и печатает результат каждого. Настоящий initserv заменит заглушку.
5. **Запланировано** (только спецификации): остальные System Libs (`<sys/*.h>`: libsync, libeventloop, libfile, libmath, libinputdevice, libv4l2), Special Libs (`<special/*.h>`: ipclib, loglib, camlib, …), System Services (initserv/PlatformRegistry, logserv, appserv, IOserv, display/compositor/window-серверы, camserv, locationserv), Application Layer.

### Соглашения сборки

- У каждого компонента свой обычный Makefile с выводом в собственный каталог `build/`. Верхние компоненты вызывают нижние через `$(MAKE) -C` (Boot → SystemLibs → CRT → SyscallWrappers).
- Весь нативный C компилируется с `-std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror` и линкуется с `-nostdlib -no-pie -Wl,-e,_start` в порядке `crt0.o <объекты> <System Libs> libpunos_crt.a libpunos_syscall.a`. Заголовки хоста и libc подключать нельзя — только заголовки PUNos.
- Структура initramfs (создаётся `build-initramfs.sh`): `/init`, `/punos/{bin,apps,config}`, `/run`, `/var/log/punos`, `/dev`, `/proc`, `/sys`, `/tmp`; исполняемые файлы ставятся с правами `0755` и владельцем `root:root`, mtime нормализуются к epoch 0.
