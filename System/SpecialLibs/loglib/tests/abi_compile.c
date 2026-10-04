#include <special/log.h>
_Static_assert(sizeof(LogStoreHandle) == 8 && sizeof(LogReaderHandle) == 8, "handles ABI");
_Static_assert(sizeof(LogStoreConfig) == 16 && __builtin_offsetof(LogStoreConfig, permissions) == 8, "config ABI");
_Static_assert(sizeof(LogRecordInput) == 40 && __builtin_offsetof(LogRecordInput, component) == 24, "input ABI");
_Static_assert(sizeof(LogRecord) == 56 && __builtin_offsetof(LogRecord, component) == 24 && __builtin_offsetof(LogRecord, message) == 40, "record ABI");
_Static_assert(sizeof(LogSeverity) == 4 && sizeof(LogReadStatus) == 4, "enum ABI");
_Static_assert(LOG_SEVERITY_TRACE == 0 && LOG_SEVERITY_DEBUG == 1 && LOG_SEVERITY_INFO == 2 && LOG_SEVERITY_WARNING == 3 && LOG_SEVERITY_ERROR == 4 && LOG_SEVERITY_FATAL == 5, "severity ABI");
_Static_assert(LOG_READ_STATUS_LOG_RECORD == 0 && LOG_READ_STATUS_END_OF_FILE == 1, "status ABI");
_Static_assert(LOG_FRAME_HEADER_SIZE == 27 && LOG_COMPONENT_MAX_LENGTH == 256 && LOG_MESSAGE_MAX_LENGTH == 16384 && LOG_FRAME_MAX_SIZE == 16667, "constants");
