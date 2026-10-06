/*
 * Runtime test of libmem against DevDoc/SystemLibs/libmem.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Check numbers are grouped by operation:
 *   1-19 allocate, 20-29 free, 30-49 protect, 50-89 reallocate,
 *   100-119 copy, 120-139 move, 140-149 set, 150-169 compare.
 *
 * Whether pages are really mapped is checked with mprotect(): on an unmapped
 * range it fails with ENOMEM. Whether protection really works is checked in a
 * child process, which must be killed by SIGSEGV when it writes to a
 * read-only block.
 */
#define CHECK_TEST_NAME "libmem"
#include "../../testing/check.h"

#include <sys/memory.h>

#define PAGE 4096
#define SIGSEGV 11

/* true if the whole page containing address is mapped. */
static bool page_is_mapped(const void *address)
{
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)(PAGE - 1);

    if (mprotect((void *)page, PAGE, PROT_READ | PROT_WRITE) == 0) {
        return true;
    }
    return errno == ENOMEM ? false : true;
}

static bool all_bytes_equal(const u8 *data, usize size, u8 value)
{
    usize index;

    for (index = 0; index < size; ++index) {
        if (data[index] != value) {
            return false;
        }
    }
    return true;
}

/* Runs a write of one byte to address in a child; true if SIGSEGV killed it. */
static bool write_faults(u8 *address)
{
    i32 status = 0;
    pid_t child = fork();

    if (child == 0) {
        *(volatile u8 *)address = 0x42;
        exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child) {
        return false;
    }
    return (status & 0x7f) == SIGSEGV;
}

static Bytes view(u8 *data, usize size)
{
    Bytes bytes = { data, size };
    return bytes;
}

static void test_allocate(void)
{
    MemoryBlock block = { (u8 *)1, 1 };
    MemoryBlock other;
    MemoryBlock scratch;

    /* Empty block without a syscall. */
    EXPECT(1, memory_allocate(0, &block) == CORE_OK);
    EXPECT(2, block.data == NULL && block.size == 0);

    /* One byte: a whole zero-filled, writable, page-aligned page. */
    EXPECT(3, memory_allocate(1, &block) == CORE_OK);
    EXPECT(4, block.data != NULL && block.size == 1);
    EXPECT(5, ((uintptr_t)block.data & (PAGE - 1)) == 0);
    EXPECT(6, all_bytes_equal(block.data, PAGE, 0));
    block.data[0] = 0xab;
    EXPECT(7, block.data[0] == 0xab);

    /* Several pages, all zero. */
    EXPECT(8, memory_allocate(3 * PAGE + 5, &other) == CORE_OK);
    EXPECT(9, other.size == 3 * PAGE + 5);
    EXPECT(10, all_bytes_equal(other.data, other.size, 0));
    EXPECT(11, other.data != block.data);

    /* Invalid arguments and impossible sizes. */
    EXPECT(12, memory_allocate(1, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(13, memory_allocate(USIZE_MAX, &scratch) == CORE_OUT_OF_MEMORY);
    EXPECT(14, memory_allocate((usize)1 << 62, &scratch) == CORE_OUT_OF_MEMORY);

    EXPECT(15, memory_free(&other) == CORE_OK);
    EXPECT(16, memory_free(&block) == CORE_OK);
}

static void test_free(void)
{
    MemoryBlock block;
    MemoryBlock empty = { NULL, 0 };
    MemoryBlock bogus;
    u8 *data;

    EXPECT(20, memory_allocate(2 * PAGE, &block) == CORE_OK);
    data = block.data;
    EXPECT(21, page_is_mapped(data) && page_is_mapped(data + PAGE));
    EXPECT(22, memory_free(&block) == CORE_OK);
    EXPECT(23, block.data == NULL && block.size == 0);
    EXPECT(24, !page_is_mapped(data) && !page_is_mapped(data + PAGE));

    /* The empty block, including a second free of the same variable. */
    EXPECT(25, memory_free(&empty) == CORE_OK);
    EXPECT(26, memory_free(&block) == CORE_OK);

    EXPECT(27, memory_free(NULL) == CORE_INVALID_ARGUMENT);
    /* Not a block produced by memory_allocate(): inconsistent or unaligned. */
    bogus.data = NULL;
    bogus.size = 8;
    EXPECT(28, memory_free(&bogus) == CORE_INVALID_ARGUMENT);
    bogus.data = data + 16;
    bogus.size = 8;
    EXPECT(29, memory_free(&bogus) == CORE_INVALID_ARGUMENT);
}

static void test_protect(void)
{
    MemoryBlock block;
    MemoryBlock empty = { NULL, 0 };

    EXPECT(30, memory_allocate(PAGE + 1, &block) == CORE_OK);
    block.data[PAGE] = 7;

    /* Read-only: reading works, writing faults on every page of the block. */
    EXPECT(31, memory_protect(&block, MEM_READ) == CORE_OK);
    EXPECT(32, block.data[PAGE] == 7);
    EXPECT(33, write_faults(block.data));
    EXPECT(34, write_faults(block.data + PAGE));

    /* Back to read-write: writing works again in this process. */
    EXPECT(35, memory_protect(&block, MEM_READ | MEM_WRITE) == CORE_OK);
    EXPECT(36, !write_faults(block.data));
    block.data[0] = 9;
    EXPECT(37, block.data[0] == 9);

    /* No access at all; a protected block can still be freed. */
    EXPECT(38, memory_protect(&block, MEM_NONE) == CORE_OK);
    EXPECT(39, memory_free(&block) == CORE_OK);

    EXPECT(40, memory_protect(&empty, MEM_READ) == CORE_OK);
    EXPECT(41, memory_protect(NULL, MEM_READ) == CORE_INVALID_ARGUMENT);
    EXPECT(42, memory_allocate(1, &block) == CORE_OK);
    EXPECT(43, memory_protect(&block, 8) == CORE_INVALID_ARGUMENT);
    EXPECT(44, memory_protect(&block, MEM_READ | 0x80000000u) == CORE_INVALID_ARGUMENT);
    EXPECT(45, memory_free(&block) == CORE_OK);
}

static void test_reallocate(void)
{
    MemoryBlock block = { NULL, 0 };
    MemoryBlock saved;
    MemoryBlock bogus = { NULL, 1 };
    usize index;
    u8 *old_data;

    /* An empty block is allocated. */
    EXPECT(50, memory_reallocate(&block, 10) == CORE_OK);
    EXPECT(51, block.data != NULL && block.size == 10);
    EXPECT(52, all_bytes_equal(block.data, 10, 0));
    for (index = 0; index < 10; ++index) {
        block.data[index] = (u8)(index + 1);
    }

    /* Shrink and grow within the same page: data kept, regained bytes zero. */
    old_data = block.data;
    EXPECT(53, memory_reallocate(&block, 4) == CORE_OK);
    EXPECT(54, block.data == old_data && block.size == 4);
    EXPECT(55, memory_reallocate(&block, 10) == CORE_OK);
    EXPECT(56, block.data == old_data && block.size == 10);
    EXPECT(57, block.data[0] == 1 && block.data[3] == 4);
    EXPECT(58, all_bytes_equal(block.data + 4, 6, 0));

    /* Grow past the page: moved to new pages, data kept, tail zero, old pages gone. */
    EXPECT(59, memory_reallocate(&block, 3 * PAGE) == CORE_OK);
    EXPECT(60, block.size == 3 * PAGE);
    EXPECT(61, block.data[0] == 1 && block.data[3] == 4);
    EXPECT(62, all_bytes_equal(block.data + 4, 3 * PAGE - 4, 0));
    EXPECT(63, block.data == old_data || !page_is_mapped(old_data));
    block.data[2 * PAGE] = 0x55;

    /* Shrink to fewer pages: data kept in place, tail pages released. */
    old_data = block.data;
    EXPECT(64, memory_reallocate(&block, PAGE + 1) == CORE_OK);
    EXPECT(65, block.data == old_data && block.size == PAGE + 1);
    EXPECT(66, block.data[0] == 1 && block.data[3] == 4);
    EXPECT(67, page_is_mapped(old_data + PAGE));
    EXPECT(68, !page_is_mapped(old_data + 2 * PAGE));

    /* A read-only block can be resized and ends up writable. */
    EXPECT(69, memory_protect(&block, MEM_READ) == CORE_OK);
    EXPECT(70, memory_reallocate(&block, 2 * PAGE + 1) == CORE_OK);
    EXPECT(71, block.data[0] == 1 && block.data[3] == 4);
    block.data[2 * PAGE] = 3;
    EXPECT(72, block.data[2 * PAGE] == 3);

    /* On error the block is unchanged and still usable. */
    saved = block;
    EXPECT(73, memory_reallocate(&block, USIZE_MAX) == CORE_OUT_OF_MEMORY);
    EXPECT(74, memory_reallocate(&block, (usize)1 << 62) == CORE_OUT_OF_MEMORY);
    EXPECT(75, block.data == saved.data && block.size == saved.size);
    EXPECT(76, block.data[0] == 1 && block.data[2 * PAGE] == 3);

    /* new_size == 0 frees the block. */
    old_data = block.data;
    EXPECT(77, memory_reallocate(&block, 0) == CORE_OK);
    EXPECT(78, block.data == NULL && block.size == 0);
    EXPECT(79, !page_is_mapped(old_data));
    EXPECT(80, memory_reallocate(&block, 0) == CORE_OK);

    EXPECT(81, memory_reallocate(NULL, 1) == CORE_INVALID_ARGUMENT);
    EXPECT(82, memory_reallocate(&bogus, 1) == CORE_INVALID_ARGUMENT);

    /* Growing a read-only block within its page must clear bytes, so it needs write access. */
    EXPECT(83, memory_allocate(10, &block) == CORE_OK);
    block.data[0] = 5;
    EXPECT(84, memory_protect(&block, MEM_READ) == CORE_OK);
    EXPECT(85, memory_reallocate(&block, 20) == CORE_OK);
    EXPECT(86, block.data[0] == 5 && all_bytes_equal(block.data + 10, 10, 0));
    block.data[19] = 6;
    EXPECT(87, block.data[19] == 6);
    EXPECT(88, memory_free(&block) == CORE_OK);
}

static void test_copy(void)
{
    u8 source[4] = { 1, 2, 3, 4 };
    u8 target[6] = { 9, 9, 9, 9, 9, 9 };
    u8 buffer[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    EXPECT(100, memory_copy(view(target, 6), view(source, 4)) == CORE_OK);
    EXPECT(101, target[0] == 1 && target[3] == 4);
    /* Bytes past source.size are not touched. */
    EXPECT(102, target[4] == 9 && target[5] == 9);

    EXPECT(103, memory_copy(view(target, 3), view(source, 4)) == CORE_INVALID_ARGUMENT);
    EXPECT(104, target[0] == 1);

    /* Overlap in either direction is rejected; adjacent ranges are fine. */
    EXPECT(105, memory_copy(view(buffer + 2, 4), view(buffer, 4)) == CORE_INVALID_ARGUMENT);
    EXPECT(106, memory_copy(view(buffer, 4), view(buffer + 2, 4)) == CORE_INVALID_ARGUMENT);
    EXPECT(107, memory_copy(view(buffer, 4), view(buffer, 4)) == CORE_INVALID_ARGUMENT);
    EXPECT(108, buffer[2] == 3 && buffer[4] == 5);
    EXPECT(109, memory_copy(view(buffer + 4, 4), view(buffer, 4)) == CORE_OK);
    EXPECT(110, buffer[4] == 1 && buffer[7] == 4);

    /* Empty views are never dereferenced, even when data is NULL. */
    EXPECT(111, memory_copy(view(NULL, 0), view(NULL, 0)) == CORE_OK);
    EXPECT(112, memory_copy(view(target, 6), view(NULL, 0)) == CORE_OK);
    EXPECT(113, memory_copy(view(NULL, 4), view(source, 4)) == CORE_INVALID_ARGUMENT);
    EXPECT(114, memory_copy(view(target, 6), view(NULL, 1)) == CORE_INVALID_ARGUMENT);
    /* A view that wraps around the address space is invalid. */
    EXPECT(115, memory_copy(view(target, 6), view((u8 *)(USIZE_MAX - 1), 4)) ==
                    CORE_INVALID_ARGUMENT);
}

static void test_move(void)
{
    u8 buffer[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    u8 other[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    u8 source[2] = { 7, 7 };

    /* Destination after source: must copy backward. */
    EXPECT(120, memory_move(view(buffer + 2, 6), view(buffer, 5)) == CORE_OK);
    EXPECT(121, buffer[0] == 1 && buffer[1] == 2 && buffer[2] == 1 &&
                    buffer[3] == 2 && buffer[4] == 3 && buffer[5] == 4 &&
                    buffer[6] == 5 && buffer[7] == 8);

    /* Destination before source: must copy forward. */
    EXPECT(122, memory_move(view(other, 8), view(other + 2, 5)) == CORE_OK);
    EXPECT(123, other[0] == 3 && other[1] == 4 && other[2] == 5 &&
                    other[3] == 6 && other[4] == 7 && other[5] == 6 &&
                    other[6] == 7 && other[7] == 8);

    /* Same range, non-overlapping ranges, empty and invalid views. */
    EXPECT(124, memory_move(view(other, 8), view(other, 8)) == CORE_OK);
    EXPECT(125, other[0] == 3 && other[7] == 8);
    EXPECT(126, memory_move(view(other, 2), view(source, 2)) == CORE_OK);
    EXPECT(127, other[0] == 7 && other[1] == 7 && other[2] == 5);
    EXPECT(128, memory_move(view(other, 1), view(source, 2)) == CORE_INVALID_ARGUMENT);
    EXPECT(129, memory_move(view(NULL, 0), view(NULL, 0)) == CORE_OK);
    EXPECT(130, memory_move(view(NULL, 2), view(source, 2)) == CORE_INVALID_ARGUMENT);
}

static void test_set(void)
{
    u8 buffer[5] = { 0, 0, 0, 0, 0 };

    EXPECT(140, memory_set(view(buffer, 4), 0xee) == CORE_OK);
    EXPECT(141, all_bytes_equal(buffer, 4, 0xee) && buffer[4] == 0);
    EXPECT(142, memory_set(view(NULL, 0), 1) == CORE_OK);
    EXPECT(143, memory_set(view(NULL, 1), 1) == CORE_INVALID_ARGUMENT);
}

static void test_compare(void)
{
    u8 left[3] = { 1, 2, 3 };
    u8 right[3] = { 1, 2, 4 };
    u8 high[1] = { 0x80 };
    u8 low[1] = { 0x01 };
    u8 longer[4] = { 1, 2, 3, 0 };
    MemoryCompareResult result = MEMORY_COMPARE_RESULT_EQUAL;

    EXPECT(150, memory_compare(view(left, 3), view(left, 3), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_EQUAL);
    EXPECT(151, memory_compare(view(left, 3), view(right, 3), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_LESS);
    EXPECT(152, memory_compare(view(right, 3), view(left, 3), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_GREATER);
    /* Bytes compare as unsigned: 0x80 is greater than 0x01. */
    EXPECT(153, memory_compare(view(high, 1), view(low, 1), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_GREATER);
    /* A prefix is less than the longer sequence. */
    EXPECT(154, memory_compare(view(left, 2), view(right, 3), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_LESS);
    EXPECT(155, memory_compare(view(right, 3), view(left, 2), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_GREATER);
    /* The first difference wins over length: {1,2,4} > {1,2,3,0}. */
    EXPECT(156, memory_compare(view(right, 3), view(longer, 4), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_GREATER);
    EXPECT(157, memory_compare(view(NULL, 0), view(NULL, 0), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_EQUAL);
    EXPECT(158, memory_compare(view(NULL, 0), view(left, 1), &result) == CORE_OK &&
                    result == MEMORY_COMPARE_RESULT_LESS);

    EXPECT(159, memory_compare(view(left, 3), view(right, 3), NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(160, memory_compare(view(NULL, 1), view(right, 1), &result) == CORE_INVALID_ARGUMENT);
}

static void test_shared_memory(void)
{
    SharedMemory memory;
    SharedMemoryMapping mapping;
    usize size = 0;

    EXPECT(170, memory_shared_create(PAGE + 7, &memory) == CORE_OK);
    EXPECT(171, memory.fd >= 0 && memory.data != NULL &&
                    memory.size == PAGE + 7 && memory.reserved == 0);
    EXPECT(172, memory_shared_get_size(memory.fd, &size) == CORE_OK &&
                    size == PAGE + 7);
    memory.data[0] = 0x12;
    memory.data[PAGE + 6] = 0x34;
    EXPECT(173, memory_shared_map_readonly(memory.fd, PAGE + 7, &mapping) == CORE_OK);
    EXPECT(174, mapping.size == PAGE + 7 && mapping.data[0] == 0x12 &&
                    mapping.data[PAGE + 6] == 0x34);
    EXPECT(175, memory_shared_map_readonly(memory.fd, PAGE, &mapping) ==
                    CORE_INVALID_ARGUMENT);
    EXPECT(176, memory_shared_unmap(&mapping) == CORE_OK &&
                    mapping.data == NULL && mapping.size == 0);
    EXPECT(177, memory_shared_close(&memory) == CORE_OK && memory.fd == -1 &&
                    memory.data == NULL && memory.size == 0);

    EXPECT(178, memory_shared_create(0, &memory) == CORE_INVALID_ARGUMENT);
    EXPECT(179, memory_shared_create(1, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(180, memory_shared_get_size(-1, &size) == CORE_INVALID_ARGUMENT);
    EXPECT(181, memory_shared_map_readonly(-1, PAGE, &mapping) ==
                    CORE_INVALID_ARGUMENT);
    EXPECT(182, memory_shared_unmap(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(183, memory_shared_close(NULL) == CORE_INVALID_ARGUMENT);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_allocate();
    test_free();
    test_protect();
    test_reallocate();
    test_copy();
    test_move();
    test_set();
    test_compare();
    test_shared_memory();

    check_passed();
    return 0;
}
