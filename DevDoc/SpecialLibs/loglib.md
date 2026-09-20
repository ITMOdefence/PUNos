## Описание

Специальная библиотека для последовательного сохранения и чтения
структурированных журнальных записей. `logserv` использует библиотеку как
низкоуровневое хранилище: он формирует записи, выбирает момент flush и ротации,
а `loglib` кодирует их в файл, восстанавливает при чтении и управляет handle
файлов журналов.

## Имя библиотеки

`special/log`

## Контракты

1. Библиотека не является сервисом и не использует IPC. Она не принимает
   подключения, не знает endpoint, не проверяет permissions и не предоставляет
   API приложениям.
2. Библиотека используется `logserv`. Выбор каталога и имени файла, правила
   хранения, фильтрация уровней, поиск по истории и момент ротации определяет
   сам `logserv`.
3. Библиотека использует только `libcore`, `libmem`, `libstr` и `libfile`.
   Типы `WallClockTime`, `ProcessId` и `ThreadId` импортируются соответственно
   из `libtime`, `libprocess` и `libthread`; библиотека не получает их сама.
4. Все операции возвращают `ErrorCode` из `libcore`. `CORE_OK` является
   единственным успешным результатом.
5. Значения параметров с префиксом `out_` определены только при возврате
   `CORE_OK`, кроме `out_status` в `log_reader_next()`: оно определено при
   `CORE_OK`, а `out_record` определён только при статусе `LogRecord`.
6. `LogStoreHandle` и `LogReaderHandle` являются непрозрачными объектами.
   Каждый открытый handle должен быть закрыт ровно один раз через
   `log_store_close()` или `log_reader_close()`.
7. `LogStoreConfig.path` копируется во время `log_store_open()` и
   `log_store_rotate()`. Вызывающий код сохраняет владение исходной строкой и
   может уничтожить её после возврата операции.
8. `LogRecordInput.component` и `LogRecordInput.message` не переходят во
   владение библиотеки и должны быть действительны только на время
   `log_store_append()`. Библиотека сериализует их до возврата функции.
9. `LogRecord`, полученный через `log_reader_next()`, владеет строками
   `component` и `message`. Вызывающий код обязан освободить их ровно один раз
   через `log_record_destroy()`.
10. Один store или reader принадлежит одному потоку. Одновременные вызовы с
    одним handle не допускаются; синхронизацию обеспечивает `logserv` через
    `libsync`.
11. `log_store_append()`, `log_store_flush()` и `log_store_rotate()` могут
    блокировать поток на файловом I/O. Выполнять их в event loop, который
    обслуживает UI или IPC, не допускается.
12. `log_store_append()` сохраняет порядок успешно добавленных записей одного
    `LogStoreHandle`.
13. Каждая запись записывается как независимый кадр. Если операция append
    завершается ошибкой ввода-вывода, файл может оканчиваться неполным кадром.
    `log_reader_next()` распознаёт такой хвост и возвращает `CORE_IO_ERROR`;
    решение о восстановлении или замене файла принимает `logserv`.
14. Ротация не выбирает новое имя файла автоматически. `logserv` передаёт
    `log_store_rotate()` путь к уже выбранному новому журналу.
15. `log_store_rotate()` сначала успешно открывает новый файл, затем закрывает
    прежний и только после этого делает новый файл текущим. Если открытие или
    закрытие завершается ошибкой, новый файл не становится текущим.

### Разрешённые зависимости

| Зависимость | Использование |
| --- | --- |
| `libcore` | Базовые типы, `Bytes`, `ErrorCode` и проверка размеров кадра |
| `libmem` | Внутренние буферы и строки записи, возвращаемой reader |
| `libstr` | Пути, component и текст записи в UTF-8 |
| `libfile` | Открытие, чтение, запись, flush и закрытие файлов журнала |
| `libtime` | Тип `WallClockTime` в записи; время получает `logserv` |
| `libprocess` | Тип `ProcessId` в записи; идентификатор задаёт `logserv` |
| `libthread` | Тип `ThreadId` в записи; идентификатор задаёт `logserv` |

### Формат кадра журнала

Файл журнала представляет собой последовательность кадров без отдельного
глобального заголовка. Все целые поля записываются в little-endian порядке.
`frame_size` включает все байты кадра, включая собственное поле.

| Смещение | Поле | Представление | Назначение |
| --- | --- | --- | --- |
| `0` | `frame_size` | `u32` | Полный размер кадра от `frame_size` до конца message |
| `4` | `timestamp` | `WallClockTime` (`u64`) | Время, назначенное `logserv`, в миллисекундах Unix epoch |
| `12` | `process_id` | `ProcessId` (`i32`) | Идентификатор процесса-источника |
| `16` | `thread_id` | `ThreadId` (`i32`) | Идентификатор потока-источника |
| `20` | `severity` | `u8` | Уровень из `LogSeverity` |
| `21` | `component_length` | `u16` | Длина component в байтах |
| `23` | `message_length` | `u32` | Длина message в байтах |
| `27` | `component` | `u8[component_length]` | UTF-8 имя компонента без `NUL` |
| `27 + component_length` | `message` | `u8[message_length]` | UTF-8 текст записи без `NUL` |

### Состояния хранилища

| Состояние | Допустимые операции | Назначение |
| --- | --- | --- |
| `Open` | `log_store_append()`, `log_store_flush()`, `log_store_rotate()`, `log_store_close()` | Файл журнала открыт для добавления кадров |
| `ReaderOpen` | `log_reader_next()`, `log_reader_close()` | Файл журнала открыт для последовательного чтения |

Операция с закрытым handle возвращает `CORE_INVALID_STATE`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| --- | --- | --- | --- |
| `LogStoreHandle` | Непрозрачный указатель: `struct LogStoreHandle *` | `NULL` или открытое хранилище | Контекст добавления записей в один файл журнала |
| `LogReaderHandle` | Непрозрачный указатель: `struct LogReaderHandle *` | `NULL` или открытый reader | Контекст последовательного чтения одного файла журнала |
| `LogStoreConfig` | Структура: `const String *path`, `FilePermissions permissions` | Корректный UTF-8 путь и права от `0000` до `0777` | Путь и права нового файла журнала |
| `LogSeverity` | Перечисление | `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal` | Важность записи, определённая `logserv` |
| `LogRecordInput` | Структура: `WallClockTime timestamp`, `ProcessId process_id`, `ThreadId thread_id`, `LogSeverity severity`, `const String *component`, `const String *message` | Корректные UTF-8 строки и допустимый уровень | Невладеющая запись для добавления в журнал |
| `LogRecord` | Структура: `WallClockTime timestamp`, `ProcessId process_id`, `ThreadId thread_id`, `LogSeverity severity`, `String component`, `String message` | Корректный кадр из журнала | Владеющая запись, прочитанная из журнала |
| `LogReadStatus` | Перечисление | `LogRecord`, `EndOfFile` | Результат одной попытки чтения кадра |

## Константы

| Наименование | Значение | Назначение |
| --- | --- | --- |
| `LOG_FRAME_HEADER_SIZE` | `27` | Размер части кадра до данных component |
| `LOG_COMPONENT_MAX_LENGTH` | `256` | Максимальная длина component в байтах |
| `LOG_MESSAGE_MAX_LENGTH` | `16384` | Максимальная длина message в байтах |
| `LOG_FRAME_MAX_SIZE` | `16667` | Максимальный полный размер одного кадра в байтах |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| --- | --- | --- | --- |
| `log_store_open(config, out_store)` | `const LogStoreConfig *config`, `LogStoreHandle *out_store` | `ErrorCode` | Открывает или создаёт файл по `config.path` в режиме `AppendCreate` и создаёт хранилище для добавления записей |
| `log_store_append(store, record)` | `LogStoreHandle store`, `const LogRecordInput *record` | `ErrorCode` | Проверяет размеры и UTF-8, сериализует одну запись в кадр и полностью добавляет его в текущий файл журнала |
| `log_store_flush(store)` | `LogStoreHandle store` | `ErrorCode` | Передаёт все ранее записанные кадры на носитель через `file_flush()` |
| `log_store_rotate(store, config)` | `LogStoreHandle store`, `const LogStoreConfig *config` | `ErrorCode` | Открывает новый файл, закрывает прежний и делает новый текущим; при ошибке новый файл не становится текущим |
| `log_store_close(store)` | `LogStoreHandle *store` | `ErrorCode` | Закрывает текущий файл без неявного `file_flush()`, освобождает внутреннее состояние и записывает `NULL` |
| `log_reader_open(path, out_reader)` | `const String *path`, `LogReaderHandle *out_reader` | `ErrorCode` | Открывает существующий файл журнала только для последовательного чтения с начала |
| `log_reader_next(reader, out_status, out_record)` | `LogReaderHandle reader`, `LogReadStatus *out_status`, `LogRecord *out_record` | `ErrorCode` | Читает и декодирует один кадр; при чистом конце файла возвращает `EndOfFile` без значения `out_record` |
| `log_record_destroy(record)` | `LogRecord *record` | `ErrorCode` | Вызывает `string_destroy()` для component и message прочитанной записи, затем делает запись недействительной |
| `log_reader_close(reader)` | `LogReaderHandle *reader` | `ErrorCode` | Закрывает файл чтения, освобождает внутреннее состояние и записывает `NULL` |

## Ошибки

| `ErrorCode` | Когда возвращается |
| --- | --- |
| `CORE_INVALID_ARGUMENT` | Передан `NULL`, пустой путь, недопустимый `LogSeverity` или значение вне контракта операции |
| `CORE_INVALID_STATE` | Операция вызвана с закрытым или недопустимым handle |
| `CORE_INVALID_PATH` | Путь журнала имеет недопустимый формат |
| `CORE_NOT_FOUND` | Файл, переданный в `log_reader_open()`, не существует |
| `CORE_OUT_OF_MEMORY` | Невозможно выделить store, reader, буфер кадра или строки прочитанной записи |
| `CORE_IO_ERROR` | Невозможно прочитать, записать, закрыть или сохранить файл; кадр повреждён или файл завершается неполным кадром |
| `CORE_OVERFLOW` | Размер component, message или полного кадра превышает установленный максимум |
