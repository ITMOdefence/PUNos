/* Linked only into the IPC allocation-failure integration-test executable. */
#include <sys/memory.h>

ErrorCode __real_memory_allocate(usize, MemoryBlock *);
ErrorCode __wrap_memory_allocate(usize size, MemoryBlock *out_block)
{
    static bool failed;
    if (size == 6 && !failed) {
        failed = true;
        return CORE_OUT_OF_MEMORY;
    }
    return __real_memory_allocate(size, out_block);
}
