# loglib

Special Lib of PUNos (`special/log`): sequential storage and retrieval of
structured log entries for `logserv`.

Specification: `DevDoc/SpecialLibs/loglib.md`.

## Build and test

From the repository root:

```bash
make -C System/SpecialLibs/loglib check
```

## Usage

```c
#include <special/log.h>

LogStoreHandle *store = NULL;
LogStoreConfig config;
String path;

string_from_c_str("/var/log/test.log", &path);
config.path = &path;
config.permissions = 0644;
log_store_open(&config, &store);

LogRecordInput record;
record.timestamp = 0;
record.process_id = 1;
record.thread_id = 1;
record.severity = LOG_SEVERITY_INFO;
record.component = &component_str;
record.message = &message_str;
log_store_append(store, &record);
log_store_close(&store);
```

## What the library provides

- **Store operations.** `log_store_open`, `log_store_append`, `log_store_flush`,
  `log_store_rotate`, `log_store_close`.
- **Reader operations.** `log_reader_open`, `log_reader_next`,
  `log_record_destroy`, `log_reader_close`.
- **Frame format.** 27-byte header + component + message.

## Implementation decisions

- **Frame header is 27 bytes.** Includes timestamp, process_id, thread_id,
  severity, component_length, message_length.
- **Records are written as independent frames.** A failed append may leave a
  partial frame at the end of the file.
- **Rotation opens new file before closing old one.** If opening fails, the
  old file stays current.

## Tests

- `tests/abi_compile.c`: compile-time checks of types and constants.
- `tests/log_runtime.c`: runtime checks of store/reader lifecycle, frame
  format, rotation and error paths.
