#include <sys/memory.h>

static bool fail_next;
ErrorCode __real_memory_allocate(usize size, MemoryBlock *out_block);
void fake_input_fail_allocation(void) { fail_next = true; }

ErrorCode __wrap_memory_allocate(usize size, MemoryBlock *out_block)
{
    if (fail_next) {
        fail_next = false;
        return CORE_OUT_OF_MEMORY;
    }
    return __real_memory_allocate(size, out_block);
}
