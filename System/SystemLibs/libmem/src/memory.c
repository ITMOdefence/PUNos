#include <sys/memory.h>

#include <punos/syscall/syscall.h>

/* Linux x86_64 base page size: the granularity of mmap(), munmap() and mprotect(). */
#define MEMORY_PAGE_SIZE ((usize)4096)

#define MEMORY_KNOWN_PROTECTION (MEM_READ | MEM_WRITE | MEM_EXECUTE)

static const MemoryBlock empty_block = { NULL, 0 };

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

/* Rounds size up to whole pages. A size that cannot be rounded cannot be mapped. */
static ErrorCode mapping_length(usize size, usize *out_length)
{
    usize padded;

    if (core_size_add(size, MEMORY_PAGE_SIZE - 1, &padded) != CORE_OK) {
        return CORE_OUT_OF_MEMORY;
    }
    *out_length = padded & ~(MEMORY_PAGE_SIZE - 1);
    return CORE_OK;
}

static bool block_is_empty(const MemoryBlock *block)
{
    return block->data == NULL && block->size == 0;
}

/*
 * A non-empty block always starts on a page boundary, because it is the start
 * of its own mapping. This rejects, among other mistakes, a Bytes view or a
 * pointer into the middle of a block passed as a MemoryBlock.
 */
static bool block_is_valid(const MemoryBlock *block)
{
    if (block_is_empty(block)) {
        return true;
    }
    return block->data != NULL && block->size != 0 &&
           ((uintptr_t)block->data & (MEMORY_PAGE_SIZE - 1)) == 0;
}

/* A view may be empty; otherwise it needs memory that does not wrap around. */
static bool bytes_is_valid(Bytes bytes)
{
    usize end;

    if (bytes.size == 0) {
        return true;
    }
    return bytes.data != NULL &&
           core_size_add((uintptr_t)bytes.data, bytes.size, &end) == CORE_OK;
}

/*
 * Converts the errno of a failed wrapper. ENOMEM from mmap() and mprotect()
 * already maps to CORE_OUT_OF_MEMORY through the common table.
 */
static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

static ErrorCode map_pages(usize length, u8 **out_data)
{
    void *mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mapping == MAP_FAILED) {
        return last_error();
    }
    *out_data = mapping;
    return CORE_OK;
}

static ErrorCode unmap_pages(u8 *data, usize length)
{
    if (munmap(data, length) != 0) {
        return last_error();
    }
    return CORE_OK;
}

static void copy_forward(u8 *destination, const u8 *source, usize size)
{
    usize index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static void copy_backward(u8 *destination, const u8 *source, usize size)
{
    while (size != 0) {
        --size;
        destination[size] = source[size];
    }
}

static void fill(u8 *destination, u8 value, usize size)
{
    usize index;

    for (index = 0; index < size; ++index) {
        destination[index] = value;
    }
}

/* ------------------------------------------------------------------------ */
/* Allocation                                                               */
/* ------------------------------------------------------------------------ */

ErrorCode memory_allocate(usize size, MemoryBlock *out_block)
{
    ErrorCode result;
    usize length;
    u8 *data;

    if (out_block == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (size == 0) {
        *out_block = empty_block;
        return CORE_OK;
    }

    result = mapping_length(size, &length);
    if (result != CORE_OK) {
        return result;
    }
    /* Anonymous mappings are zero-filled by the kernel. */
    result = map_pages(length, &data);
    if (result != CORE_OK) {
        return result;
    }
    out_block->data = data;
    out_block->size = size;
    return CORE_OK;
}

ErrorCode memory_reallocate(MemoryBlock *block, usize new_size)
{
    ErrorCode result;
    usize old_length;
    usize new_length;
    u8 *data;

    if (block == NULL || !block_is_valid(block)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (new_size == 0) {
        return memory_free(block);
    }
    if (block_is_empty(block)) {
        return memory_allocate(new_size, block);
    }

    result = mapping_length(new_size, &new_length);
    if (result != CORE_OK) {
        return result;
    }
    /* The block was mapped, so its own length always rounds without overflow. */
    (void)mapping_length(block->size, &old_length);

    /* The block may have been protected; the data below is read and written. */
    if (mprotect(block->data, old_length, PROT_READ | PROT_WRITE) != 0) {
        return last_error();
    }

    if (new_length == old_length) {
        /*
         * Same pages. The bytes past the old size may hold data from an earlier
         * shrink, so bytes gained by growing are cleared explicitly.
         */
        if (new_size > block->size) {
            fill(block->data + block->size, 0, new_size - block->size);
        }
        block->size = new_size;
        return CORE_OK;
    }

    if (new_length < old_length) {
        /* Fewer pages: return the tail pages to the kernel, keep the data in place. */
        result = unmap_pages(block->data + new_length, old_length - new_length);
        if (result != CORE_OK) {
            return result;
        }
        block->size = new_size;
        return CORE_OK;
    }

    /*
     * More pages: move to a new, zero-filled mapping. The old mapping is
     * released only after the copy, so on any failure the block is intact.
     */
    result = map_pages(new_length, &data);
    if (result != CORE_OK) {
        return result;
    }
    copy_forward(data, block->data, block->size);
    result = unmap_pages(block->data, old_length);
    if (result != CORE_OK) {
        (void)unmap_pages(data, new_length);
        return result;
    }
    block->data = data;
    block->size = new_size;
    return CORE_OK;
}

ErrorCode memory_free(MemoryBlock *block)
{
    ErrorCode result;
    usize length;

    if (block == NULL || !block_is_valid(block)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (block_is_empty(block)) {
        return CORE_OK;
    }

    (void)mapping_length(block->size, &length);
    result = unmap_pages(block->data, length);
    if (result != CORE_OK) {
        return result;
    }
    *block = empty_block;
    return CORE_OK;
}

ErrorCode memory_protect(MemoryBlock *block, MemoryProtection protection)
{
    usize length;
    i32 flags = PROT_NONE;

    if (block == NULL || !block_is_valid(block) ||
        (protection & ~MEMORY_KNOWN_PROTECTION) != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (block_is_empty(block)) {
        return CORE_OK;
    }

    if (protection & MEM_READ) {
        flags |= PROT_READ;
    }
    if (protection & MEM_WRITE) {
        flags |= PROT_WRITE;
    }
    if (protection & MEM_EXECUTE) {
        flags |= PROT_EXEC;
    }

    (void)mapping_length(block->size, &length);
    if (mprotect(block->data, length, flags) != 0) {
        return last_error();
    }
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Byte operations                                                          */
/* ------------------------------------------------------------------------ */

ErrorCode memory_copy(Bytes destination, Bytes source)
{
    uintptr_t target;
    uintptr_t origin;

    if (!bytes_is_valid(destination) || !bytes_is_valid(source) ||
        source.size > destination.size) {
        return CORE_INVALID_ARGUMENT;
    }
    if (source.size == 0) {
        return CORE_OK;
    }

    /* Only the first source.size bytes of destination are written. */
    target = (uintptr_t)destination.data;
    origin = (uintptr_t)source.data;
    if (target < origin + source.size && origin < target + source.size) {
        return CORE_INVALID_ARGUMENT;
    }

    copy_forward(destination.data, source.data, source.size);
    return CORE_OK;
}

ErrorCode memory_move(Bytes destination, Bytes source)
{
    if (!bytes_is_valid(destination) || !bytes_is_valid(source) ||
        source.size > destination.size) {
        return CORE_INVALID_ARGUMENT;
    }
    if (source.size == 0 || destination.data == source.data) {
        return CORE_OK;
    }

    /* Copy away from the overlap so no source byte is overwritten before it is read. */
    if ((uintptr_t)destination.data < (uintptr_t)source.data) {
        copy_forward(destination.data, source.data, source.size);
    } else {
        copy_backward(destination.data, source.data, source.size);
    }
    return CORE_OK;
}

ErrorCode memory_set(Bytes destination, u8 value)
{
    if (!bytes_is_valid(destination)) {
        return CORE_INVALID_ARGUMENT;
    }
    fill(destination.data, value, destination.size);
    return CORE_OK;
}

ErrorCode memory_compare(Bytes left, Bytes right, MemoryCompareResult *out_result)
{
    usize common;
    usize index;

    if (out_result == NULL || !bytes_is_valid(left) || !bytes_is_valid(right)) {
        return CORE_INVALID_ARGUMENT;
    }

    common = left.size < right.size ? left.size : right.size;
    for (index = 0; index < common; ++index) {
        if (left.data[index] != right.data[index]) {
            *out_result = left.data[index] < right.data[index]
                              ? MEMORY_COMPARE_RESULT_LESS
                              : MEMORY_COMPARE_RESULT_GREATER;
            return CORE_OK;
        }
    }

    if (left.size < right.size) {
        *out_result = MEMORY_COMPARE_RESULT_LESS;
    } else if (left.size > right.size) {
        *out_result = MEMORY_COMPARE_RESULT_GREATER;
    } else {
        *out_result = MEMORY_COMPARE_RESULT_EQUAL;
    }
    return CORE_OK;
}
