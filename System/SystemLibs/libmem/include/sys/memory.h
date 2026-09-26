#ifndef PUNOS_SYS_MEMORY_H
#define PUNOS_SYS_MEMORY_H

/*
 * libmem (sys/memory) - allocation, release and protection of memory blocks,
 * plus copying, filling and comparing byte ranges.
 *
 * Specification: DevDoc/SystemLibs/libmem.md
 *
 * Memory comes straight from the kernel: every non-empty MemoryBlock is its
 * own anonymous private mmap() mapping, rounded up to whole 4 KiB pages. There
 * is no heap and no sub-allocation. This keeps memory_protect() exact (it can
 * never affect a neighbouring block) and makes every allocation zero-filled,
 * at the cost of at least one page per non-empty block. Callers that need many
 * small objects should group them into one block.
 *
 * The only syscall wrappers used are mmap(), munmap() and mprotect().
 * All operations return ErrorCode; see <sys/core.h> for the conventions.
 */

#include <sys/core.h>

/*
 * Owning handle of an allocated memory block.
 *
 * data points to size usable bytes. An empty block is { NULL, 0 }; it owns no
 * memory and all operations accept it. Only blocks produced by this library
 * are valid: never build a MemoryBlock by hand or from a Bytes view.
 *
 * A block is released exactly once with memory_free(). Copying the structure
 * does not duplicate the memory: after one copy is freed, all other copies are
 * invalid.
 */
typedef struct MemoryBlock {
    u8 *data;
    usize size;
} MemoryBlock;

/* Bit mask of MEM_* flags. */
typedef u32 MemoryProtection;

#define MEM_NONE ((MemoryProtection)0)
#define MEM_READ ((MemoryProtection)1)
#define MEM_WRITE ((MemoryProtection)2)
#define MEM_EXECUTE ((MemoryProtection)4)

/* Result of memory_compare(). The values follow the sign of left - right. */
typedef enum MemoryCompareResult {
    MEMORY_COMPARE_RESULT_LESS = -1,
    MEMORY_COMPARE_RESULT_EQUAL = 0,
    MEMORY_COMPARE_RESULT_GREATER = 1
} MemoryCompareResult;

_Static_assert(sizeof(MemoryBlock) == 16, "MemoryBlock ABI mismatch");
_Static_assert(sizeof(MemoryCompareResult) == 4, "MemoryCompareResult ABI mismatch");

/*
 * Allocates a zero-filled block of size bytes with MEM_READ | MEM_WRITE
 * access and stores it in *out_block.
 *
 * size == 0 yields the empty block { NULL, 0 } without a syscall.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_block is NULL; CORE_OUT_OF_MEMORY if
 * the kernel cannot provide the memory or size is too large to map.
 */
ErrorCode memory_allocate(usize size, MemoryBlock *out_block);

/*
 * Resizes *block to new_size bytes. The first min(old size, new size) bytes
 * are preserved; bytes added by growing are zero. block->data may change.
 *
 * - An empty block is allocated as by memory_allocate().
 * - new_size == 0 frees the block and sets it to { NULL, 0 }.
 * - Otherwise the block ends up with MEM_READ | MEM_WRITE access, whatever
 *   protection it had before.
 *
 * On error *block is left unchanged and remains valid.
 *
 * Errors: CORE_INVALID_ARGUMENT if block is NULL or not a valid block;
 * CORE_OUT_OF_MEMORY if the new size cannot be provided; other codes from
 * core_error_from_errno() if the kernel rejects the operation.
 */
ErrorCode memory_reallocate(MemoryBlock *block, usize new_size);

/*
 * Releases *block and sets it to { NULL, 0 }. Freeing the empty block is a
 * no-op. Works whatever protection the block has.
 *
 * On error *block is left unchanged.
 *
 * Errors: CORE_INVALID_ARGUMENT if block is NULL or not a valid block; other
 * codes from core_error_from_errno() if munmap() fails.
 */
ErrorCode memory_free(MemoryBlock *block);

/*
 * Sets the access rights of every page of *block to protection, a
 * combination of MEM_READ, MEM_WRITE and MEM_EXECUTE, or MEM_NONE.
 * On x86_64 MEM_WRITE and MEM_EXECUTE also allow reading.
 * Changing the protection of the empty block is a no-op.
 *
 * Errors: CORE_INVALID_ARGUMENT if block is NULL, not a valid block or
 * protection contains unknown bits; other codes from core_error_from_errno()
 * if mprotect() fails.
 */
ErrorCode memory_protect(MemoryBlock *block, MemoryProtection protection);

/*
 * Copies source.size bytes from source to the start of destination.
 * The ranges must not overlap; use memory_move() when they may.
 *
 * Errors: CORE_INVALID_ARGUMENT if source is larger than destination, if the
 * ranges overlap, or if a view has data == NULL with a non-zero size.
 */
ErrorCode memory_copy(Bytes destination, Bytes source);

/*
 * Copies source.size bytes from source to the start of destination; the
 * ranges may overlap.
 *
 * Errors: CORE_INVALID_ARGUMENT if source is larger than destination or if a
 * view has data == NULL with a non-zero size.
 */
ErrorCode memory_move(Bytes destination, Bytes source);

/*
 * Sets every byte of destination to value.
 *
 * Errors: CORE_INVALID_ARGUMENT if destination has data == NULL with a
 * non-zero size.
 */
ErrorCode memory_set(Bytes destination, u8 value);

/*
 * Compares left and right lexicographically as unsigned bytes and stores the
 * result in *out_result. When one view is a prefix of the other, the shorter
 * one is less; two empty views are equal.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_result is NULL or if a view has
 * data == NULL with a non-zero size.
 */
ErrorCode memory_compare(Bytes left, Bytes right, MemoryCompareResult *out_result);

#endif
