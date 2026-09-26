/*
 * Compile-time checks of the libmem ABI against DevDoc/SystemLibs/libmem.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/memory.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

/* MemoryBlock has the same layout as Bytes: data, then size. */
_Static_assert(__builtin_offsetof(MemoryBlock, data) == 0, "MemoryBlock.data offset");
_Static_assert(__builtin_offsetof(MemoryBlock, size) == 8, "MemoryBlock.size offset");
_Static_assert(SAME_TYPE(((MemoryBlock *)0)->data, u8 *), "MemoryBlock.data type");
_Static_assert(SAME_TYPE(((MemoryBlock *)0)->size, usize), "MemoryBlock.size type");

/* MemoryProtection is a u32 bit mask with one bit per right. */
_Static_assert(SAME_TYPE((MemoryProtection)0, u32), "MemoryProtection must be u32");
_Static_assert(SAME_TYPE(MEM_READ, MemoryProtection), "MEM_* type");
_Static_assert(MEM_NONE == 0, "MEM_NONE");
_Static_assert(MEM_READ == 1 && MEM_WRITE == 2 && MEM_EXECUTE == 4, "MEM_* values");

/* MemoryCompareResult values are ABI. */
_Static_assert(MEMORY_COMPARE_RESULT_LESS == -1, "LESS");
_Static_assert(MEMORY_COMPARE_RESULT_EQUAL == 0, "EQUAL");
_Static_assert(MEMORY_COMPARE_RESULT_GREATER == 1, "GREATER");
