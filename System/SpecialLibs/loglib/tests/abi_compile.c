/*
 * Compile-time checks of the loglib ABI against DevDoc/SpecialLibs/loglib.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <special/log.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((LogSeverity)0, u32), "LogSeverity size");
_Static_assert(SAME_TYPE((LogReadStatus)0, u32), "LogReadStatus size");
_Static_assert(sizeof(LogStoreConfig) == 16, "LogStoreConfig ABI");
_Static_assert(sizeof(LogRecordInput) == 40, "LogRecordInput ABI");
_Static_assert(sizeof(LogRecord) == 56, "LogRecord ABI");

_Static_assert(LOG_FRAME_HEADER_SIZE == 27, "LOG_FRAME_HEADER_SIZE");
_Static_assert(LOG_COMPONENT_MAX_LENGTH == 256, "LOG_COMPONENT_MAX_LENGTH");
_Static_assert(LOG_MESSAGE_MAX_LENGTH == 16384, "LOG_MESSAGE_MAX_LENGTH");
_Static_assert(LOG_FRAME_MAX_SIZE == 16667, "LOG_FRAME_MAX_SIZE");

_Static_assert(LOG_SEVERITY_TRACE == 0, "LOG_SEVERITY_TRACE");
_Static_assert(LOG_SEVERITY_DEBUG == 1, "LOG_SEVERITY_DEBUG");
_Static_assert(LOG_SEVERITY_INFO == 2, "LOG_SEVERITY_INFO");
_Static_assert(LOG_SEVERITY_WARNING == 3, "LOG_SEVERITY_WARNING");
_Static_assert(LOG_SEVERITY_ERROR == 4, "LOG_SEVERITY_ERROR");
_Static_assert(LOG_SEVERITY_FATAL == 5, "LOG_SEVERITY_FATAL");

_Static_assert(LOG_READ_STATUS_LOG_RECORD == 0, "LOG_READ_STATUS_LOG_RECORD");
_Static_assert(LOG_READ_STATUS_END_OF_FILE == 1, "LOG_READ_STATUS_END_OF_FILE");

int main(void)
{
    return 0;
}
