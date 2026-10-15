/*
 * A fake V4L2 kernel for the libv4l2 runtime test.
 *
 * libv4l2 reaches the driver through open/ioctl/mmap, so most of it can only be
 * exercised on a machine that has a /dev/video* node, and the interesting
 * failures - a device that is not V4L2 at all, one that cannot stream, one that
 * disappears mid-capture - cannot be produced on demand even when it does.
 *
 * This file replaces those five wrappers so the real libv4l2 code runs against
 * a scripted device. It is linked ahead of libpunos_syscall.a, which is why the
 * Makefile passes -Wl,--allow-multiple-definition: that archive keeps every
 * wrapper in one object, so anything that pulls in mmap() also drags in ioctl().
 *
 * The ioctl request numbers are the ones libv4l2 computes in src/v4l2.c; they
 * are reproduced here so a change to either side shows up as a test failure
 * rather than as a silently unanswered request.
 */

#include <sys/core.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/memory.h>

/* --- the driver's ABI, as libv4l2 spells it out ------------------------- */

#define FAKE_BUF_TYPE_VIDEO_CAPTURE 1u
#define FAKE_MEMORY_MMAP 1u

#define FAKE_CAP_VIDEO_CAPTURE 0x00000001u
#define FAKE_CAP_STREAMING 0x04000000u
#define FAKE_CAP_DEVICE_CAPS 0x80000000u

struct FakeCapability {
    u8 driver[16];
    u8 card[32];
    u8 bus_info[32];
    u32 version;
    u32 capabilities;
    u32 device_caps;
    u32 reserved[3];
};

struct FakeFmtdesc {
    u32 index;
    u32 type;
    u32 flags;
    u8 description[32];
    u32 pixelformat;
    u32 mbus_code;
    u32 reserved[3];
};

struct FakeFrmsizeenum {
    u32 index;
    u32 pixel_format;
    u32 type;
    u32 size[6];
    u32 reserved[2];
};

struct FakeFrmivalenum {
    u32 index;
    u32 pixel_format;
    u32 width;
    u32 height;
    u32 type;
    u32 interval[6];
    u32 reserved[2];
};

struct FakePixFormat {
    u32 width;
    u32 height;
    u32 pixelformat;
    u32 field;
    u32 bytesperline;
    u32 sizeimage;
    u32 colorspace;
    u32 priv;
    u32 flags;
    u32 ycbcr_or_hsv_enc;
    u32 quantization;
    u32 xfer_func;
};

struct FakeFormat {
    u32 type;
    u32 reserved_padding;
    union {
        struct FakePixFormat pix;
        u8 raw_data[200];
    } fmt;
};

struct FakeRequestBuffers {
    u32 count;
    u32 type;
    u32 memory;
    u32 capabilities;
    u8 flags;
    u8 reserved[3];
};

struct FakeBuffer {
    u32 index;
    u32 type;
    u32 bytesused;
    u32 flags;
    u32 field;
    i64 timestamp_sec;
    i64 timestamp_usec;
    u32 timecode[4];
    u32 sequence;
    u32 memory;
    u64 m;
    u32 length;
    u32 reserved2;
    u32 request_fd_or_reserved;
};

struct FakeStreamParm {
    u32 type;
    union {
        struct {
            u32 capability;
            u32 capturemode;
            u32 timeperframe_numerator;
            u32 timeperframe_denominator;
            u32 extendedmode;
            u32 readbuffers;
            u32 reserved[4];
        } capture;
        u8 raw_data[200];
    } parm;
};

#define FAKE_IOC_ENCODE(dir, type, nr, size)                                \
    (((u64)(dir) << 30) | ((u64)(type) << 8) | ((u64)(nr) << 0) |         \
     ((u64)(size) << 16))

#define REQ_QUERYCAP FAKE_IOC_ENCODE(2u, 'V', 0, sizeof(struct FakeCapability))
#define REQ_ENUM_FMT FAKE_IOC_ENCODE(3u, 'V', 2, sizeof(struct FakeFmtdesc))
#define REQ_ENUM_FRAMESIZES FAKE_IOC_ENCODE(3u, 'V', 74, sizeof(struct FakeFrmsizeenum))
#define REQ_ENUM_FRAMEINTERVALS FAKE_IOC_ENCODE(3u, 'V', 75, sizeof(struct FakeFrmivalenum))
#define REQ_TRY_FMT FAKE_IOC_ENCODE(3u, 'V', 64, sizeof(struct FakeFormat))
#define REQ_S_FMT FAKE_IOC_ENCODE(3u, 'V', 5, sizeof(struct FakeFormat))
#define REQ_G_PARM FAKE_IOC_ENCODE(3u, 'V', 21, sizeof(struct FakeStreamParm))
#define REQ_S_PARM FAKE_IOC_ENCODE(3u, 'V', 22, sizeof(struct FakeStreamParm))
#define REQ_REQBUFS FAKE_IOC_ENCODE(3u, 'V', 8, sizeof(struct FakeRequestBuffers))
#define REQ_QUERYBUF FAKE_IOC_ENCODE(3u, 'V', 9, sizeof(struct FakeBuffer))
#define REQ_QBUF FAKE_IOC_ENCODE(3u, 'V', 15, sizeof(struct FakeBuffer))
#define REQ_DQBUF FAKE_IOC_ENCODE(3u, 'V', 17, sizeof(struct FakeBuffer))
#define REQ_STREAMON FAKE_IOC_ENCODE(1u, 'V', 18, sizeof(u32))
#define REQ_STREAMOFF FAKE_IOC_ENCODE(1u, 'V', 19, sizeof(u32))

#define FAKE_NODE_PATH "/dev/video0"
#define FAKE_FD ((i32)4242)
#define FAKE_FRAME_BYTES ((u32)61440)
#define FAKE_BUFFER_COUNT ((u32)4)

/* --- what the test scripts ----------------------------------------------- */

static bool device_gone;      /* ioctl() answers ENODEV */
static bool not_v4l2;         /* ioctl() answers ENOTTY: not a V4L2 device */
static bool cannot_stream;    /* QUERYCAP without VIDEO_CAPTURE|STREAMING */
static bool one_frame_ready;  /* DQBUF hands out a buffer */
static bool close_fails;
static bool fractional_rate;
static u32 requested_interval_numerator;
static u32 requested_interval_denominator;
static u32 dq_sequence;
static u32 streamon_calls;
static u32 next_buffer_index;
static u32 next_buffer_size = FAKE_FRAME_BYTES;
static bool queued[FAKE_BUFFER_COUNT];
static bool fail_streamoff;
static i32 fail_queue_index = -1;
static u32 requested_buffer_count = FAKE_BUFFER_COUNT;

void fake_v4l2_fail_streamoff(bool value) { fail_streamoff = value; }
void fake_v4l2_fail_queue(i32 index) { fail_queue_index = index; }
void fake_v4l2_buffer_count(u32 count) { requested_buffer_count = count; }


/*
 * Static stand-in for the MMAP buffers. Two constraints shape it:
 *  - it cannot come from libmem, because libmem allocates with mmap() and the
 *    fake would recurse until the stack ran out;
 *  - every handout has to be page-aligned, because libmem validates a block by
 *    requiring data to be page-aligned, on the grounds that a block is always
 *    its own mapping.
 */
#define FAKE_PAGE_SIZE ((usize)4096)
/*
 * Big enough for every mapping the whole test asks for. The pool is never
 * reset: libmem serves the test's own Strings from it too, so handing the same
 * pages out twice would overwrite a path the test is still using. Four capture
 * buffers per device at 60 KiB each, over several devices, plus the rest.
 */
#define FAKE_POOL_BYTES ((usize)(8u << 20))
static u8 mmap_pool[FAKE_POOL_BYTES] __attribute__((aligned(FAKE_PAGE_SIZE)));
static usize mmap_pool_used;

void fake_v4l2_reset(void)
{
    device_gone = false;
    not_v4l2 = false;
    cannot_stream = false;
    one_frame_ready = false;
    close_fails = false;
    fractional_rate = false;
    requested_interval_numerator = 0;
    requested_interval_denominator = 0;
    dq_sequence = 1;
    streamon_calls = 0;
    next_buffer_index = 0;
    next_buffer_size = FAKE_FRAME_BYTES;
    fail_streamoff = false;
    fail_queue_index = -1;
    requested_buffer_count = FAKE_BUFFER_COUNT;
    for (u32 i = 0; i < FAKE_BUFFER_COUNT; ++i) { queued[i] = false; }
}

void fake_v4l2_buffer(u32 index, u32 size)
{
    next_buffer_index = index;
    next_buffer_size = size;
    one_frame_ready = true;
}

void fake_v4l2_set_device_gone(bool gone) { device_gone = gone; }
void fake_v4l2_set_not_v4l2(bool value) { not_v4l2 = value; }
void fake_v4l2_set_cannot_stream(bool value) { cannot_stream = value; }
void fake_v4l2_arm_frame(bool ready) { one_frame_ready = ready; }
void fake_v4l2_set_close_fails(bool value) { close_fails = value; }
void fake_v4l2_set_fractional_rate(bool value) { fractional_rate = value; }
u32 fake_v4l2_requested_interval_numerator(void) { return requested_interval_numerator; }
u32 fake_v4l2_requested_interval_denominator(void) { return requested_interval_denominator; }
u32 fake_v4l2_streamon_calls(void) { return streamon_calls; }

/* --- the wrappers libv4l2 uses ------------------------------------------- */

/* The one path this fake pretends to have a node behind. */
static bool path_exists(const char *path)
{
    usize index;

    if (path == NULL) {
        return false;
    }
    for (index = 0; index < sizeof(FAKE_NODE_PATH) - 1; ++index) {
        if (path[index] != FAKE_NODE_PATH[index]) {
            return false;
        }
    }
    return path[sizeof(FAKE_NODE_PATH) - 1] == '\0';
}

i32 open(const char *path, i32 flags, mode_t mode)
{
    (void)flags;
    (void)mode;

    /* A path with no node behind it still has to fail the way the kernel makes
     * it fail, so the caller sees CORE_NOT_FOUND rather than a made-up device.
     * Whether the node that does exist is really V4L2 is decided by ioctl(). */
    if (!path_exists(path)) {
        errno = ENOENT;
        return -1;
    }
    return FAKE_FD;
}

i32 close(i32 fd)
{
    (void)fd;
    if (close_fails) {
        errno = EIO;
        return -1;
    }
    return 0;
}

void *mmap(void *address, size_t length, i32 protection, i32 flags, i32 fd, i64 offset)
{
    (void)address;
    (void)protection;
    (void)flags;
    (void)fd;
    (void)offset;

    /* Round the length up so the next handout stays page-aligned. */
    usize rounded = (length + FAKE_PAGE_SIZE - 1) & ~(FAKE_PAGE_SIZE - 1);

    if (rounded > sizeof(mmap_pool) - mmap_pool_used) {
        return MAP_FAILED;
    }
    {
        void *base = &mmap_pool[mmap_pool_used];

        mmap_pool_used += rounded;
        /* An anonymous mapping is zero-filled, and libmem depends on it: it
         * hands out blocks without initialising them and callers rely on the
         * contents being zero. A reused static pool is not, so zero it here
         * or every "already false/0 from the allocation" assumption breaks. */
        (void)memory_set((Bytes){ (u8 *)base, rounded }, 0);
        return base;
    }
}

/* libmem calls mprotect() to undo a protection before resizing a block. The
 * pool is ordinary static memory rather than a real mapping, so the kernel
 * would refuse; accepting it keeps libmem's own bookkeeping exercised. */
i32 mprotect(void *address, size_t length, i32 protection)
{
    (void)address;
    (void)length;
    (void)protection;
    return 0;
}

i32 munmap(void *address, size_t length)
{
    (void)address;
    (void)length;
    /* The pool is static and lives for the whole test run. */
    return 0;
}

i32 ioctl(i32 fd, u64 request, unsigned long argument)
{
    (void)fd;

    /* Anything at all fails once the device is gone. */
    if (device_gone) {
        errno = ENODEV;
        return -1;
    }
    if (not_v4l2) {
        errno = ENOTTY;
        return -1;
    }

    switch (request) {
    case REQ_QUERYCAP: {
        struct FakeCapability *capability = (struct FakeCapability *)argument;

        memory_set((Bytes){ (u8 *)capability, sizeof(*capability) }, 0);
        /* Older drivers leave device_caps at zero, in which case the general
         * bits are the whole truth; this one sets both. */
        capability->capabilities = FAKE_CAP_VIDEO_CAPTURE | FAKE_CAP_DEVICE_CAPS;
        capability->device_caps = cannot_stream
                                      ? FAKE_CAP_VIDEO_CAPTURE
                                      : (FAKE_CAP_VIDEO_CAPTURE | FAKE_CAP_STREAMING);
        return 0;
    }

    case REQ_ENUM_FMT: {
        struct FakeFmtdesc *desc = (struct FakeFmtdesc *)argument;

        if (desc->index > 0) {
            errno = EINVAL; /* end of the format list */
            return -1;
        }
        desc->pixelformat = 0x56595559u; /* YUYV */
        return 0;
    }

    case REQ_ENUM_FRAMESIZES: {
        struct FakeFrmsizeenum *size = (struct FakeFrmsizeenum *)argument;

        if (size->index > 0) {
            errno = EINVAL;
            return -1;
        }
        size->type = 1; /* discrete */
        size->size[0] = 640;
        size->size[1] = 480;
        return 0;
    }

    case REQ_ENUM_FRAMEINTERVALS: {
        struct FakeFrmivalenum *ival = (struct FakeFrmivalenum *)argument;

        if (ival->index > 0) {
            errno = EINVAL;
            return -1;
        }
        ival->type = 1; /* discrete */
        ival->interval[0] = fractional_rate ? 1001 : 1;
        ival->interval[1] = fractional_rate ? 30000 : 30;
        return 0;
    }

    case REQ_TRY_FMT:
    case REQ_S_FMT: {
        struct FakeFormat *format = (struct FakeFormat *)argument;

        format->fmt.pix.sizeimage = FAKE_FRAME_BYTES;
        return 0;
    }

    case REQ_S_PARM: {
        struct FakeStreamParm *parm = (struct FakeStreamParm *)argument;

        requested_interval_numerator = parm->parm.capture.timeperframe_numerator;
        requested_interval_denominator = parm->parm.capture.timeperframe_denominator;
        parm->parm.capture.timeperframe_numerator = fractional_rate ? 1001 : 1;
        parm->parm.capture.timeperframe_denominator = fractional_rate ? 30000 : 30;
        return 0;
    }

    case REQ_G_PARM: {
        struct FakeStreamParm *parm = (struct FakeStreamParm *)argument;

        parm->parm.capture.timeperframe_numerator = fractional_rate ? 1001 : 1;
        parm->parm.capture.timeperframe_denominator = fractional_rate ? 30000 : 30;
        return 0;
    }

    case REQ_REQBUFS: {
        struct FakeRequestBuffers *reqbufs = (struct FakeRequestBuffers *)argument;

        for (u32 i = 0; i < FAKE_BUFFER_COUNT; ++i) { queued[i] = false; }
        if (reqbufs->count != 0) { reqbufs->count = requested_buffer_count; }
        return 0;
    }

    case REQ_QUERYBUF: {
        struct FakeBuffer *buffer = (struct FakeBuffer *)argument;

        buffer->length = FAKE_FRAME_BYTES;
        buffer->m = (u64)buffer->index * (u64)FAKE_FRAME_BYTES;
        return 0;
    }

    case REQ_QBUF: {
        struct FakeBuffer *buffer = (struct FakeBuffer *)argument;
        if ((i32)buffer->index == fail_queue_index) {
            fail_queue_index = -1;
            errno = EIO;
            return -1;
        }
        if (buffer->index >= FAKE_BUFFER_COUNT || queued[buffer->index]) {
            errno = EINVAL;
            return -1;
        }
        queued[buffer->index] = true;
        return 0;
    }

    case REQ_DQBUF: {
        struct FakeBuffer *buffer = (struct FakeBuffer *)argument;

        if (!one_frame_ready) {
            errno = EAGAIN;
            return -1;
        }
        one_frame_ready = false;
        buffer->index = next_buffer_index;
        buffer->bytesused = next_buffer_size;
        buffer->sequence = dq_sequence;
        if (buffer->index < FAKE_BUFFER_COUNT) { queued[buffer->index] = false; }
        dq_sequence += 1;
        return 0;
    }

    case REQ_STREAMON:
        streamon_calls += 1;
        return 0;

    case REQ_STREAMOFF:
        if (fail_streamoff) { errno = EIO; return -1; }
        for (u32 i = 0; i < FAKE_BUFFER_COUNT; ++i) { queued[i] = false; }
        return 0;

    default:
        errno = ENOTTY;
        return -1;
    }
}
