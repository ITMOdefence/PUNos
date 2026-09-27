# Сборка и запуск системного слоя PUNos

Этот каталог содержит воспроизводимое окружение для подготовки, сборки и
запуска базового ядра PUNos в виртуальной машине QEMU.

На текущем этапе PUNos использует неизменённое upstream-ядро Linux для
архитектуры x86_64. Исходный код Linux, скачанные архивы и результаты сборки
не хранятся в Git: их создают сценарии из `System/scripts`.

## Текущее состояние

Сейчас поддерживается следующая контрольная точка:

1. Скачать и проверить исходники зафиксированной версии Linux.
2. Восстановить эталонную конфигурацию ядра PUNos.
3. Собрать `bzImage`.
4. Собрать Syscall Wrappers, CRT, System Libs, `/init` и воспроизводимый
   initramfs.
5. Запустить ядро и первый userspace PUNos в QEMU.

Настоящий initserv и остальные System Services пока не реализованы. В образ
включается явно обозначенный initserv smoke-test stub, подтверждающий работу
границы `/init` → `fork()` → `execve()`.

## Быстрый запуск

После установки зависимостей выполните из корня репозитория:

```bash
./System/scripts/prepare-kernel.sh
./System/scripts/build-kernel.sh
./System/scripts/build-initramfs.sh
./System/scripts/run-qemu-initramfs.sh
```

Первые три команды подготавливают локальные артефакты, последняя запускает
PUNos в QEMU. При повторном запуске скачивать исходники ядра заново не нужно.
Все команды, кроме установки системных пакетов, выполняются без `sudo`.

## Зафиксированное окружение

| Компонент | Значение |
| --- | --- |
| Ядро | Linux 6.12.111 LTS |
| Архитектура | x86_64 |
| Виртуальная машина | QEMU `q35` |
| Ускорение | TCG |
| Виртуальные CPU | 2 |
| Оперативная память | 512 МиБ |
| Консоль | COM1 (`ttyS0`, 115200 бод) |

Точная версия ядра хранится в `System/kernel/version`, контрольная сумма архива
— в `System/kernel/SHA256SUMS`, а эталонная конфигурация — в
`System/kernel/configs/x86_64.config`.

## Требования

Инструкция проверена на Debian 13. Команды установки рассчитаны на Debian и
Ubuntu; совместимость с конкретным выпуском Ubuntu необходимо подтверждать в
CI команды. Требуется x86_64-компьютер и не менее 4 ГиБ свободного места.

Под воспроизводимостью v1 понимается одинаковый результат из одинаковых
входных файлов на эталонном окружении Debian 13 с совместимым toolchain.
Сборка пока не герметична: точные версии GCC, binutils, cpio и gzip не
закреплены контейнером или образом виртуальной машины. Поэтому побайтовое
совпадение артефактов между разными дистрибутивами пока не гарантируется.

Установите необходимые пакеты:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  bc \
  bison \
  flex \
  git \
  perl \
  python3 \
  libssl-dev \
  libelf-dev \
  libncurses-dev \
  dwarves \
  cpio \
  curl \
  xz-utils \
  qemu-system-x86 \
  qemu-utils
```

Сценарии следует запускать из корня клонированного репозитория PUNos. Они вычисляют
пути относительно собственного расположения, поэтому репозиторий может
находиться в любом каталоге.

Только установка пакетов выполняется через `sudo`. Сценарии PUNos, Git и QEMU
следует запускать от обычного пользователя, которому принадлежит клон:

```bash
./System/scripts/prepare-kernel.sh
./System/scripts/build-kernel.sh
./System/scripts/build-initramfs.sh
./System/scripts/run-qemu-initramfs.sh
```

Не используйте `sudo` перед этими командами. Иначе сгенерированные файлы будут
принадлежать root, а Git может остановиться с ошибкой `dubious ownership`.

## 1. Подготовка исходников ядра

Из корня репозитория выполните:

```bash
./System/scripts/prepare-kernel.sh
```

Сценарий:

1. Читает зафиксированную версию ядра.
2. Скачивает архив с `cdn.kernel.org`, если архив ещё не существует.
3. Проверяет SHA-256 архива.
4. Распаковывает исходники в `System/linux-6.12.111`.
5. Создаёт локальные каталоги `System/linux-build` и `System/rootfs`.

Архив сначала загружается во временный файл с расширением `.part` и становится
рабочим только после успешного завершения загрузки. Повторный запуск безопасен:
существующий архив снова проверяется, а версия уже распакованного дерева
проверяется перед повторным использованием.

После успешного выполнения локальная структура содержит:

```text
System/
├── download/
│   └── linux-6.12.111.tar.xz
├── kernel/
├── linux-6.12.111/
├── linux-build/
├── rootfs/
└── scripts/
```

Каталоги `download`, `linux-6.12.111`, `linux-build` и `rootfs` игнорируются
Git.

## 2. Сборка ядра

Выполните:

```bash
./System/scripts/build-kernel.sh
```

Сценарий копирует эталонную конфигурацию в отдельный каталог сборки, применяет
значения Kconfig для Linux 6.12.111 и собирает `bzImage`. Дерево исходников при
этом не загрязняется результатами компиляции.

По умолчанию сборка использует все доступные логические процессоры. Чтобы
ограничить параллелизм, задайте переменную `JOBS`:

```bash
JOBS=4 ./System/scripts/build-kernel.sh
```

Готовое ядро располагается здесь:

```text
System/linux-build/arch/x86/boot/bzImage
```

Каталог `System/linux-build` является локальным и игнорируется Git.

## 3. Необязательная проверка голого ядра

Этот шаг нужен только для диагностики ядра и не является обычным запуском
PUNos. После успешной сборки выполните:

```bash
./System/scripts/run-qemu.sh
```

QEMU запускается без графического окна. Загрузочный журнал ядра выводится в
текущий терминал через serial-консоль.

В начале журнала должна присутствовать версия с суффиксом PUNos:

```text
Linux version 6.12.111-punos
```

Затем ядро завершит загрузку сообщением, похожим на:

```text
Kernel panic - not syncing: VFS: Unable to mount root fs
```

Это ожидаемо: сценарий пока не передаёт QEMU корневую файловую систему или
initramfs.

Для выхода из QEMU нажмите `Ctrl+A`, отпустите клавиши, затем нажмите `X`.

## 4. Обычный запуск PUNos

Соберите userspace и архив:

```bash
./System/scripts/build-initramfs.sh
```

Сценарий создаёт staging-дерево `System/rootfs` и архив:

```text
System/images/punos-initramfs.cpio.gz
```

После этого запустите полный текущий образ:

```bash
./System/scripts/run-qemu-initramfs.sh
```

Успешная загрузка содержит следующие сообщения:

```text
PUNos init: userspace started
PUNos init: filesystems ready
PUNos init: initserv started
PUNos initserv stub: smoke-test process is running
full syscall wrappers runtime test passed
PUNos initserv stub: syscall self-test passed
libcore runtime test passed
PUNos initserv stub: libcore self-test passed
libmath runtime test passed
PUNos initserv stub: libmath self-test passed
libmem runtime test passed
PUNos initserv stub: libmem self-test passed
libstr runtime test passed
PUNos initserv stub: libstr self-test passed
libfile runtime test passed
PUNos initserv stub: libfile self-test passed
libtime runtime test passed
PUNos initserv stub: libtime self-test passed
libprocess runtime test passed
PUNos initserv stub: libprocess self-test passed
libthread runtime test passed
PUNos initserv stub: libthread self-test passed
libsync runtime test passed
PUNos initserv stub: libsync self-test passed
libeventloop runtime test passed
PUNos initserv stub: libeventloop self-test passed
libinputdevice runtime test passed
PUNos initserv stub: libinputdevice self-test passed
libv4l2 runtime test passed
PUNos initserv stub: libv4l2 self-test passed
```

Stub по очереди запускает self-test'ы из `/punos/bin/*-selftest` и сообщает
результат каждого. Строка `... self-test failed` означает, что тест упал;
номер упавшей проверки выводится строкой выше.

Виртуальная машина продолжает работать, поскольку PID 1 и stub остаются
живыми. Для выхода нажмите `Ctrl+A`, затем `X`.

Smoke-test stub занимает путь `/punos/bin/initserv` только для проверки
загрузочной цепочки. Команда, разрабатывающая настоящий initserv, заменяет этот
исполняемый файл на этапе интеграции своего слоя; остальным командам изменять
stub для запуска текущего образа не требуется.

## Проверка NativeRuntime перед интеграцией

Команда следующего слоя может независимо проверить публичную границу CRT и
Syscall Wrappers из корня репозитория:

```bash
make -C System/NativeRuntime/SyscallWrappers check
make -C System/NativeRuntime/CRT check
```

Первая команда выполняет ABI-проверки и freestanding runtime-тесты всех групп
wrappers. Вторая собирает минимальный статический ELF, проверяет вход в
`main()`, готовность thread-local `errno` и завершение через `exit()`.

Полная интеграционная проверка выполняется командами:

```bash
./System/scripts/build-initramfs.sh
./System/scripts/run-qemu-initramfs.sh
```

Она подтверждает работу той же границы на зафиксированном ядре Linux
6.12.111-punos. Настоящий `initserv` и System Services не являются частью этой
приёмки.

## Проверка System Libs

System Libs собираются и тестируются одной командой из корня репозитория:

```bash
make -C System/SystemLibs check
```

Команда для каждой библиотеки собирает compile-time проверки ABI и запускает
runtime-тест на хосте. Отдельная библиотека проверяется так же, например
`make -C System/SystemLibs/libcore check`. Те же runtime-тесты входят в
initramfs и выполняются при загрузке в QEMU (см. раздел 4). Соглашения по
структуре библиотек и тестов описаны в `System/SystemLibs/README.md`.

## Проверка воспроизводимости

После подготовки и сборки выполните из корня репозитория:

```bash
make -s -C System/linux-6.12.111 \
  O="$PWD/System/linux-build" kernelrelease
test -s System/linux-build/arch/x86/boot/bzImage
```

Первая команда должна вывести `6.12.111-punos`, а вторая — завершиться с кодом
`0`. Финальная ручная проверка — запуск `run-qemu.sh`: версия ядра должна
совпасть, а загрузка должна дойти до ожидаемого сообщения об отсутствии rootfs.

Для проверки userspace дополнительно соберите initramfs и убедитесь, что QEMU
выводит контрольные сообщения из предыдущего раздела. Это подтверждает
текущий boot-срез, но не готовность настоящего initserv или System Services.

Для проверки воспроизводимости initramfs выполните две сборки и сравните
контрольные суммы:

```bash
./System/scripts/build-initramfs.sh
sha256sum System/images/punos-initramfs.cpio.gz
./System/scripts/build-initramfs.sh
sha256sum System/images/punos-initramfs.cpio.gz
```

Обе строки SHA-256 должны совпасть.

Архив initramfs можно проверить без QEMU:

```bash
gzip -t System/images/punos-initramfs.cpio.gz
gzip -dc System/images/punos-initramfs.cpio.gz | cpio -itv
```

Первая команда должна завершиться с кодом `0`; во второй должны присутствовать
`init`, `punos/bin/initserv` и self-test'ы `punos/bin/*-selftest`
(`syscall`, `libcore`, `libmath`, `libmem`, `libstr`, `libfile`, `libtime`,
`libprocess`, `libthread`, `libsync`, `libeventloop`, `libinputdevice`,
`libv4l2`) с владельцем `root:root` и правами `0755`.

## Рабочая структура

```text
System/
├── start-guide.md
├── kernel/
│   ├── version
│   ├── SHA256SUMS
│   └── configs/
│       └── x86_64.config
├── scripts/
│   ├── prepare-kernel.sh
│   ├── build-kernel.sh
│   ├── build-initramfs.sh
│   ├── run-qemu.sh
│   └── run-qemu-initramfs.sh
├── Boot/                     # исходники /init и smoke-test initserv
├── NativeRuntime/            # CRT и Syscall Wrappers
├── SystemLibs/               # System Libs (libcore, libmath, libmem, libstr, libfile, libtime, libprocess, libthread, libsync, libeventloop, libinputdevice, libv4l2)
├── images/                   # генерируется, Git игнорирует
├── download/                 # генерируется, Git игнорирует
├── linux-6.12.111/           # генерируется, Git игнорирует
├── linux-build/              # генерируется, Git игнорирует
└── rootfs/                   # staging initramfs, Git игнорирует
```

## Типичные ошибки

### `Kernel sources not found`

Сначала подготовьте исходники:

```bash
./System/scripts/prepare-kernel.sh
```

### `FAILED` при проверке SHA-256

Скачанный архив повреждён или не соответствует зафиксированной версии. Удалите
только локальный архив и повторите подготовку:

```bash
rm System/download/linux-6.12.111.tar.xz
./System/scripts/prepare-kernel.sh
```

### `Kernel image not found`

Перед запуском QEMU необходимо собрать ядро:

```bash
./System/scripts/build-kernel.sh
```

### `Initramfs image not found`

Соберите userspace и архив перед полным запуском:

```bash
./System/scripts/build-initramfs.sh
```

### QEMU запущен, но сообщений `PUNos init` нет

Убедитесь, что используется `run-qemu-initramfs.sh`, а не диагностический
`run-qemu.sh`. Затем пересоберите архив и проверьте его содержимое командами из
раздела проверки воспроизводимости.

### Сборка завершилась из-за нехватки памяти

Уменьшите количество параллельных задач:

```bash
JOBS=2 ./System/scripts/build-kernel.sh
```

### `detected dubious ownership`

Команда Git запущена не тем пользователем, которому принадлежит репозиторий.
Если приглашение терминала начинается с `root@`, выйдите из root-shell:

```bash
exit
```

Затем повторите `git status` от обычного пользователя. Не добавляйте проект в
глобальный `safe.directory` пользователя root: это скрывает причину, но не
исправляет владельца файлов.

Если только генерируемый каталог сборки был случайно создан через `sudo`,
верните его текущему пользователю:

```bash
sudo chown -R "$(id -un)":"$(id -gn)" System/linux-build
```

### `Permission denied` внутри `System/linux-build`

Не продолжайте сборку через `sudo`. Проверьте владельца:

```bash
find System/linux-build -maxdepth 2 ! -user "$(id -un)" -print | head
```

Если показаны файлы от другого пользователя, исправьте владельца командой из
предыдущего раздела и повторите сборку.

### Неполный каталог исходников

Если подготовка сообщает `Existing kernel source directory is incomplete`,
переместите повреждённый локальный каталог в сторону и запустите подготовку
заново:

```bash
mv System/linux-6.12.111 System/linux-6.12.111.incomplete
./System/scripts/prepare-kernel.sh
```

## Граница ответственности этого окружения

Этот репозиторный срез предоставляет зафиксированное ядро, Syscall Wrappers,
CRT, реализованные System Libs, `/init`, сборку initramfs и запуск в QEMU. Реализация настоящего initserv,
PlatformRegistry и последующих System Services принадлежит соответствующим
командам. Smoke-test stub существует только как проверяемая точка передачи
управления следующему слою.
