## Описание

Системная библиотека для работы с файлами и каталогами.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libfile`. Готовы:

- публичный заголовок `<sys/file.h>` со всеми типами и константами;
- статическая библиотека `libpunos_file.a` со всеми операциями из таблицы
  ниже;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест (`tests/file_runtime.c`), который создаёт, читает, пишет,
  переименовывает и удаляет файлы в `/tmp`, создаёт каталог и проверяет
  `FileKind` для обычного файла, каталога и `/dev/null`. Выполняется на
  хосте командой `make -C System/SystemLibs/libfile check` и внутри PUNos
  в QEMU как `/punos/bin/libfile-selftest`.

Библиотека вызывает только `open()`, `close()`, `read()`, `write()`,
`lseek()`, `fstat()`, `mkdir()`, `unlink()`, `rename()` и `fsync()`
(контракт 1); `FileHandle` не отдаёт файловый дескриптор вызывающему коду
(контракт 5). У спецификации нет операции удаления каталога (`rmdir()` не
входит в список разрешённых wrappers), поэтому каталог, созданный
`directory_create()`, этой библиотекой удалить нельзя. Решения, принятые
при реализации, описаны в `System/SystemLibs/libfile/README.md`.

## Имя библиотеки
`sys/file`
## Контракты

1. Библиотека использует только Syscall Wrappers `open()`, `close()`, `read()`,
   `write()`, `lseek()`, `fstat()`, `mkdir()`, `unlink()`, `rename()` и `fsync()`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. Значения результата передаются через параметры с префиксом `out_`.
4. При возврате ошибки значение в `out_*` не определено и не используется.
5. `FileHandle` — непрозрачный указатель. Внутренний файловый дескриптор не
   передаётся вызывающему коду; `file_open()` создаёт объект и записывает
   указатель в `out_file`.
6. Открытый `FileHandle` должен быть закрыт ровно один раз через `file_close()`.
7. Пути передаются как UTF-8 строки без символа `NUL`.
8. `file_read()` может вернуть меньше байтов, чем размер буфера. Возврат `0`
   означает конец файла.
9. `file_write_all()` завершает запись всех переданных байтов либо возвращает
   ошибку.
10. `file_flush()` гарантирует передачу записанных данных в Syscall Wrapper
   `fsync()`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `String` | Тип из `libstr` | UTF-8 строка без `NUL` | Путь к файлу или каталогу |
| `Bytes` | Тип из `libcore`: `u8 *data`, `usize size` | Блок байтов длиной от `0` | Невладеющий буфер для чтения и записи |
| `FileHandle` | Непрозрачный указатель: `struct FileHandle *` | `NULL` или только открытый файл | Идентификатор открытого файла |
| `FileOpenMode` | Перечисление | `Read`, `WriteTruncate`, `ReadWriteCreate`, `AppendCreate` | Режим открытия файла |
| `FileSeekOrigin` | Перечисление | `Start`, `Current`, `End` | Точка отсчёта смещения в файле |
| `FilePermissions` | Беззнаковое целое | Права доступа от `0000` до `0777` | Права нового файла или каталога |
| `FileKind` | Перечисление | `Regular`, `Directory`, `Device`, `Other` | Тип файла |
| `FileInfo` | Структура: `u64 size`, `FileKind kind` | Размер неотрицателен | Метаданные открытого файла |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `FILE_DEFAULT_PERMISSIONS` | Права доступа по умолчанию для нового файла: `0644` |
| `DIRECTORY_DEFAULT_PERMISSIONS` | Права доступа по умолчанию для нового каталога: `0755` |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `file_open(path, mode, permissions, out_file)` | `const String *path`, `FileOpenMode mode`, `FilePermissions permissions`, `FileHandle *out_file` | `ErrorCode` | Открывает существующий файл или создаёт новый через `open()` |
| `file_close(file)` | `FileHandle *file` | `ErrorCode` | Закрывает файл через `close()` и делает handle недействительным |
| `file_read(file, buffer, out_read)` | `FileHandle file`, `Bytes buffer`, `usize *out_read` | `ErrorCode` | Читает данные через `read()`; `0` байтов в `out_read` означает конец файла |
| `file_write_all(file, data)` | `FileHandle file`, `Bytes data` | `ErrorCode` | Записывает все данные через один или несколько вызовов `write()` |
| `file_seek(file, offset, origin, out_position)` | `FileHandle file`, `i64 offset`, `FileSeekOrigin origin`, `u64 *out_position` | `ErrorCode` | Изменяет позицию чтения или записи через `lseek()` |
| `file_info(file, out_info)` | `FileHandle file`, `FileInfo *out_info` | `ErrorCode` | Получает размер и тип файла через `fstat()` |
| `file_flush(file)` | `FileHandle file` | `ErrorCode` | Сохраняет записанные данные через `fsync()` |
| `directory_create(path, permissions)` | `const String *path`, `FilePermissions permissions` | `ErrorCode` | Создаёт каталог через `mkdir()` |
| `file_remove(path)` | `const String *path` | `ErrorCode` | Удаляет файл через `unlink()` |
| `file_rename(old_path, new_path)` | `const String *old_path`, `const String *new_path` | `ErrorCode` | Переименовывает или перемещает файл через `rename()` |
