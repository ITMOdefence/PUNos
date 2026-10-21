#include <sys/memory.h>

/* Allocation accounting checks the libmem contract, including sizes that
 * round to the same number of pages and would otherwise hide a bad free. */
static MemoryBlock live[128];
static usize mismatches;

ErrorCode __real_memory_allocate(usize size, MemoryBlock *out_block);
ErrorCode __real_memory_free(MemoryBlock *block);

usize fake_memory_mismatches(void) { return mismatches; }
usize fake_memory_live(void)
{
    usize count = 0;
    for (usize i = 0; i < 128; ++i) {
        if (live[i].data != NULL) { ++count; }
    }
    return count;
}

ErrorCode __wrap_memory_allocate(usize size, MemoryBlock *out_block)
{
    ErrorCode result = __real_memory_allocate(size, out_block);
    if (result == CORE_OK && size != 0) {
        for (usize i = 0; i < 128; ++i) {
            if (live[i].data == NULL) {
                live[i] = *out_block;
                return result;
            }
        }
        ++mismatches;
    }
    return result;
}

ErrorCode __wrap_memory_free(MemoryBlock *block)
{
    if (block != NULL && block->data != NULL) {
        bool found = false;
        for (usize i = 0; i < 128; ++i) {
            if (live[i].data == block->data) {
                found = true;
                if (live[i].size != block->size) { ++mismatches; }
                live[i].data = NULL;
                break;
            }
        }
        if (!found) { ++mismatches; }
    }
    return __real_memory_free(block);
}
