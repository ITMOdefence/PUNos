#include <sys/input-device.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

/*
 * Hand-written Linux evdev ABI: PUNos is freestanding and has no
 * <linux/input.h> to include. Everything below matches the kernel's uapi
 * headers (checked against /usr/include/linux/input.h and
 * /usr/include/linux/input-event-codes.h on x86_64) exactly.
 */

/* struct input_event on 64-bit Linux: a 16-byte struct timeval (two longs)
 * that this library never looks at (contract 9: it supplies its own
 * monotonic timestamp instead), then type/code/value. */
struct RawInputEvent {
    i64 tv_sec;
    i64 tv_usec;
    u16 type;
    u16 code;
    i32 value;
};

_Static_assert(sizeof(struct RawInputEvent) == 24, "struct RawInputEvent ABI mismatch");

/* struct input_id from <linux/input.h>. */
struct RawInputId {
    u16 bustype;
    u16 vendor;
    u16 product;
    u16 version;
};

_Static_assert(sizeof(struct RawInputId) == 8, "struct RawInputId ABI mismatch");

/*
 * Linux's generic ioctl request encoding (asm-generic/ioctl.h), which this
 * library cannot include either. dir/type/nr/size are packed into one
 * request number; _IOR is a read-only request whose size is the size of
 * the structure the kernel writes back.
 */
#define IOC_NRSHIFT 0
#define IOC_TYPESHIFT (IOC_NRSHIFT + 8)
#define IOC_SIZESHIFT (IOC_TYPESHIFT + 8)
#define IOC_DIRSHIFT (IOC_SIZESHIFT + 14)
#define IOC_READ 2u

#define IOC_ENCODE(dir, type, nr, size)                                     \
    (((u64)(dir) << IOC_DIRSHIFT) | ((u64)(type) << IOC_TYPESHIFT) |         \
     ((u64)(nr) << IOC_NRSHIFT) | ((u64)(size) << IOC_SIZESHIFT))

/* EVIOCGID: _IOR('E', 0x02, struct input_id) */
#define EVIOCGID_REQUEST IOC_ENCODE(IOC_READ, 'E', 0x02, sizeof(struct RawInputId))
/* EVIOCGBIT(0, len): _IOC(_IOC_READ, 'E', 0x20 + 0, len) - event TYPE bits
 * (EV_SYN, EV_KEY, EV_REL, EV_ABS, ...), not a specific type's codes: one
 * bit per event type is all this library needs to report capabilities. */
#define EVIOCGBIT_TYPES_REQUEST(len) IOC_ENCODE(IOC_READ, 'E', 0x20, (len))
/* EVIOCGBIT(EV_KEY, len): _IOC(_IOC_READ, 'E', 0x20 + EV_KEY, len) - one bit
 * per supported key code. EV_KEY is 1, hence nr 0x21. */
#define EVIOCGBIT_KEYS_REQUEST(len) IOC_ENCODE(IOC_READ, 'E', 0x21, (len))

#define EV_KEY 1u
#define EV_REL 2u
#define EV_ABS 3u

struct InputRawDeviceHandle {
    MemoryBlock block;
    i32 fd;
    InputRawDeviceInfo info; /* captured once, at open() time */
    /* Reused across input_raw_device_read() calls and only ever grown, the
     * same way libeventloop avoids mmap()/munmap() on every wait. */
    MemoryBlock scratch_block;
    usize scratch_capacity; /* in InputRawEvent-sized slots */
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

/* First BTN_* code (BTN_MISC). Everything below it is a KEY_* code. */
#define BTN_MISC_CODE 0x100
/* KEY_MAX is 0x2ff, so 96 bytes cover every key code a device can report. */
#define KEY_CODE_BITS_BYTES 96
#define KEY_MAX_CODE 0x2ff

/*
 * Splits the device's EV_KEY codes into keyboard keys and pointer buttons.
 *
 * The kernel numbers KEY_* below BTN_MISC (0x100) and BTN_* from there up, so
 * one pass over the bitmap tells the two apart. EV_KEY alone does not: a mouse
 * reports buttons through EV_KEY as well, and treating that as a keyboard is
 * what made a mouse classify as both.
 */
static ErrorCode read_key_capabilities(i32 fd, InputRawCapabilityMask *out_capabilities)
{
    u8 key_bits[KEY_CODE_BITS_BYTES];
    bool has_keyboard_key = false;
    bool has_button = false;
    usize index;

    for (index = 0; index < sizeof(key_bits); ++index) {
        key_bits[index] = 0;
    }
    if (ioctl(fd, EVIOCGBIT_KEYS_REQUEST(sizeof(key_bits)), (unsigned long)(uintptr_t)key_bits) < 0) {
        return last_error();
    }

    for (index = 0; index < sizeof(key_bits) * 8u; ++index) {
        u32 code = (u32)index;

        if (code > KEY_MAX_CODE) {
            break;
        }
        if ((key_bits[index / 8u] & (u8)(1u << (index % 8u))) == 0) {
            continue;
        }
        if (code < BTN_MISC_CODE) {
            has_keyboard_key = true;
        } else {
            has_button = true;
        }
    }

    *out_capabilities = 0;
    if (has_keyboard_key) {
        *out_capabilities |= INPUT_RAW_CAP_KEYBOARD_KEYS;
    }
    if (has_button) {
        *out_capabilities |= INPUT_RAW_CAP_POINTER_BUTTONS;
    }
    return CORE_OK;
}

static ErrorCode read_capabilities(i32 fd, InputRawCapabilityMask *out_capabilities)
{
    u32 type_bits = 0;

    if (ioctl(fd, EVIOCGBIT_TYPES_REQUEST(sizeof(type_bits)), (unsigned long)(uintptr_t)&type_bits) <
        0) {
        return last_error();
    }
    *out_capabilities = 0;
    if ((type_bits & (1u << EV_KEY)) != 0) {
        InputRawCapabilityMask key_capabilities = 0;

        *out_capabilities |= INPUT_RAW_CAP_KEY;
        if (read_key_capabilities(fd, &key_capabilities) == CORE_OK) {
            *out_capabilities |= key_capabilities;
        }
    }
    if ((type_bits & (1u << EV_REL)) != 0) {
        *out_capabilities |= INPUT_RAW_CAP_RELATIVE;
    }
    if ((type_bits & (1u << EV_ABS)) != 0) {
        *out_capabilities |= INPUT_RAW_CAP_ABSOLUTE;
    }
    return CORE_OK;
}

static ErrorCode ensure_scratch_capacity(struct InputRawDeviceHandle *device, usize capacity)
{
    usize new_size;
    ErrorCode result;

    if (capacity <= device->scratch_capacity) {
        return CORE_OK;
    }
    if (core_size_multiply(capacity, sizeof(struct RawInputEvent), &new_size) != CORE_OK) {
        return CORE_OVERFLOW;
    }
    result = memory_reallocate(&device->scratch_block, new_size);
    if (result != CORE_OK) {
        return result;
    }
    device->scratch_capacity = capacity;
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Operations                                                               */
/* ------------------------------------------------------------------------ */

ErrorCode input_raw_device_open(const InputRawDeviceConfig *config,
                                InputRawDeviceHandle *out_device)
{
    const char *path_data;
    i32 fd;
    struct RawInputId id;
    InputRawCapabilityMask capabilities = 0;
    MemoryBlock handle_block;
    struct InputRawDeviceHandle *device;
    ErrorCode result;

    if (config == NULL || out_device == NULL ||
        string_c_str(config->path, &path_data) != CORE_OK) {
        return CORE_INVALID_ARGUMENT;
    }

    fd = open(path_data, O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    if (fd < 0) {
        return last_error();
    }

    if (ioctl(fd, EVIOCGID_REQUEST, (unsigned long)(uintptr_t)&id) < 0) {
        result = last_error();
        goto fail_fd;
    }
    result = read_capabilities(fd, &capabilities);
    if (result != CORE_OK) {
        goto fail_fd;
    }

    result = memory_allocate(sizeof(struct InputRawDeviceHandle), &handle_block);
    if (result != CORE_OK) {
        goto fail_fd;
    }

    device = (struct InputRawDeviceHandle *)handle_block.data;
    device->block = handle_block;
    device->fd = fd;
    device->info.bus = id.bustype;
    device->info.vendor = id.vendor;
    device->info.product = id.product;
    device->info.version = id.version;
    device->info.capabilities = capabilities;
    /* scratch_block / scratch_capacity are already zero from the
     * zero-filled allocation. */

    *out_device = device;
    return CORE_OK;

fail_fd:
    (void)close(fd);
    return result;
}

ErrorCode input_raw_device_get_fd(InputRawDeviceHandle device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = device->fd;
    return CORE_OK;
}

ErrorCode input_raw_device_get_info(InputRawDeviceHandle device, InputRawDeviceInfo *out_info)
{
    if (device == NULL || out_info == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_info = device->info;
    return CORE_OK;
}

ErrorCode input_raw_device_read(InputRawDeviceHandle device, InputRawEvent *out_events,
                                usize events_capacity, InputRawReadStatus *out_status,
                                usize *out_count)
{
    struct RawInputEvent *raw;
    ssize_t result;
    usize count;
    usize i;
    MonotonicTime now;
    ErrorCode error;

    if (device == NULL || out_status == NULL ||
        (events_capacity != 0 && (out_events == NULL || out_count == NULL))) {
        return CORE_INVALID_ARGUMENT;
    }
    if (events_capacity == 0) {
        *out_status = INPUT_RAW_READ_STATUS_NO_EVENTS;
        return CORE_OK;
    }

    error = ensure_scratch_capacity(device, events_capacity);
    if (error != CORE_OK) {
        return error;
    }
    raw = (struct RawInputEvent *)device->scratch_block.data;

    result = read(device->fd, raw, events_capacity * sizeof(struct RawInputEvent));
    if (result < 0) {
        if (errno == EAGAIN) {
            *out_status = INPUT_RAW_READ_STATUS_NO_EVENTS;
            return CORE_OK;
        }
        if (errno == ENODEV) {
            *out_status = INPUT_RAW_READ_STATUS_DEVICE_LOST;
            return CORE_OK;
        }
        return last_error();
    }
    if (result == 0) {
        *out_status = INPUT_RAW_READ_STATUS_NO_EVENTS;
        return CORE_OK;
    }

    /* evdev only ever returns whole struct input_event records. */
    count = (usize)result / sizeof(struct RawInputEvent);
    error = time_monotonic_now(&now);
    if (error != CORE_OK) {
        return error;
    }
    for (i = 0; i < count; ++i) {
        out_events[i].type = raw[i].type;
        out_events[i].code = raw[i].code;
        out_events[i].value = raw[i].value;
        out_events[i].timestamp = now; /* one reading per read() batch, see README */
    }
    *out_status = INPUT_RAW_READ_STATUS_EVENTS_READY;
    *out_count = count;
    return CORE_OK;
}

ErrorCode input_raw_device_close(InputRawDeviceHandle *device)
{
    struct InputRawDeviceHandle *handle;
    MemoryBlock scratch_block;
    MemoryBlock handle_block;
    ErrorCode result = CORE_OK;

    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *device;
    if (close(handle->fd) != 0) {
        result = last_error();
    }
    scratch_block = handle->scratch_block;
    (void)memory_free(&scratch_block);
    handle_block = handle->block;
    (void)memory_free(&handle_block);
    *device = NULL;
    return result;
}
