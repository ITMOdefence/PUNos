| Библиотека                      | Назначение                                                                                      | Импорт                  |
| ------------------------------- | ----------------------------------------------------------------------------------------------- | ----------------------- |
| [displaylib](displaylib.md)     | Управление дисплеями и выводом кадров; контракт ещё не зафиксирован.                            | `<special/display.h>`   |
| [ipclib](ipclib.md)             | Локальный IPC: Unix sockets, кадрирование сообщений и неблокирующие соединения.                 | `<special/ipc.h>`       |
| [camlib](camlib.md)             | Захват сырых кадров камеры для `camserv` поверх `sys/v4l2`.                                     | `<special/camera.h>`    |
| [inputlib](inputlib.md)         | Кадры физических событий устройств ввода для `IOserv` поверх `sys/input-device`.                | `<special/input.h>`     |
| [loglib](loglib.md)             | Последовательное хранение и чтение структурированных журналов для `logserv`.                    | `<special/log.h>`       |
| [gpslib](gpslib.md)             | WIP<br><br>Приём и разбор GPS-данных; протокол и контракт ещё не зафиксированы.                 | `<special/gps.h>`       |
| [directionlib](directionlib.md) | WIP<br><br>Назначение и контракт ещё не зафиксированы.                                          | `<special/direction.h>` |

