#include <sys/v4l2.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

/*
 * Hand-written V4L2 ABI: PUNos is freestanding and has no
 * <linux/videodev2.h> to include. Every struct and ioctl request number
 * below was checked against /usr/include/linux/videodev2.h on the build
 * host (sizes and field offsets, via a small host-side program using the
 * real header) so the encoded ioctl request numbers - which bake the
 * struct size in - match exactly what a real V4L2 driver expects.
 */

#define V4L2_BUF_TYPE_VIDEO_CAPTURE 1u
#define V4L2_FIELD_NONE 1u
#define V4L2_MEMORY_MMAP 1u
#define V4L2_FRMSIZE_TYPE_DISCRETE 1u
#define V4L2_FRMIVAL_TYPE_DISCRETE 1u

struct RawV4l2Capability {
    u8 driver[16];
    u8 card[32];
    u8 bus_info[32];
    u32 version;
    u32 capabilities;
    u32 device_caps;
    u32 reserved[3];
};

_Static_assert(sizeof(struct RawV4l2Capability) == 104, "RawV4l2Capability ABI mismatch");

struct RawV4l2Fmtdesc {
    u32 index;
    u32 type;
    u32 flags;
    u8 description[32];
    u32 pixelformat;
    u32 mbus_code;
    u32 reserved[3];
};

_Static_assert(sizeof(struct RawV4l2Fmtdesc) == 64, "RawV4l2Fmtdesc ABI mismatch");

struct RawV4l2Frmsizeenum {
    u32 index;
    u32 pixel_format;
    u32 type;
    u32 size[6]; /* union { discrete{w,h}; stepwise{min_w,max_w,step_w,min_h,max_h,step_h}; }: 24 bytes */
    u32 reserved[2];
};

_Static_assert(sizeof(struct RawV4l2Frmsizeenum) == 44, "RawV4l2Frmsizeenum ABI mismatch");

struct RawV4l2Frmivalenum {
    u32 index;
    u32 pixel_format;
    u32 width;
    u32 height;
    u32 type;
    u32 interval[6]; /* union { discrete{num,den}; stepwise{min,max,step (2 each)}; }: 24 bytes */
    u32 reserved[2];
};

_Static_assert(sizeof(struct RawV4l2Frmivalenum) == 52, "RawV4l2Frmivalenum ABI mismatch");

struct RawV4l2PixFormat {
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

_Static_assert(sizeof(struct RawV4l2PixFormat) == 48, "RawV4l2PixFormat ABI mismatch");

struct RawV4l2Format {
    u32 type;
    /* The real struct's union is 8-byte aligned because of an unrelated,
     * unused member (a pointer inside struct v4l2_window); this padding
     * reproduces that so the total size still matches (208 bytes). */
    u32 reserved_padding;
    union {
        struct RawV4l2PixFormat pix;
        u8 raw_data[200];
    } fmt;
};

_Static_assert(sizeof(struct RawV4l2Format) == 208, "RawV4l2Format ABI mismatch");

struct RawV4l2RequestBuffers {
    u32 count;
    u32 type;
    u32 memory;
    u32 capabilities;
    u8 flags;
    u8 reserved[3];
};

_Static_assert(sizeof(struct RawV4l2RequestBuffers) == 20, "RawV4l2RequestBuffers ABI mismatch");

struct RawV4l2Buffer {
    u32 index;
    u32 type;
    u32 bytesused;
    u32 flags;
    u32 field;
    i64 timestamp_sec;  /* struct timeval; unused by this library */
    i64 timestamp_usec;
    u32 timecode[4];     /* struct v4l2_timecode; unused */
    u32 sequence;
    u32 memory;
    u64 m;                /* union m; only the low 32 bits (.offset) are used */
    u32 length;
    u32 reserved2;
    u32 request_fd_or_reserved;
};

_Static_assert(sizeof(struct RawV4l2Buffer) == 88, "RawV4l2Buffer ABI mismatch");
_Static_assert(__builtin_offsetof(struct RawV4l2Buffer, timestamp_sec) == 24,
               "RawV4l2Buffer.timestamp_sec offset");
_Static_assert(__builtin_offsetof(struct RawV4l2Buffer, sequence) == 56,
               "RawV4l2Buffer.sequence offset");
_Static_assert(__builtin_offsetof(struct RawV4l2Buffer, memory) == 60,
               "RawV4l2Buffer.memory offset");
_Static_assert(__builtin_offsetof(struct RawV4l2Buffer, m) == 64, "RawV4l2Buffer.m offset");
_Static_assert(__builtin_offsetof(struct RawV4l2Buffer, length) == 72,
               "RawV4l2Buffer.length offset");

struct RawV4l2StreamParm {
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

_Static_assert(sizeof(struct RawV4l2StreamParm) == 204, "RawV4l2StreamParm ABI mismatch");

/*
 * Linux's generic ioctl request encoding (asm-generic/ioctl.h). _IOWR
 * requests are both written to and read from by the kernel: every V4L2
 * ioctl this library uses is a read/write request except VIDIOC_STREAMON/
 * VIDIOC_STREAMOFF (write-only, an int).
 */
#define IOC_TYPESHIFT 8
#define IOC_SIZESHIFT 16
#define IOC_DIRSHIFT 30
#define IOC_WRITE 1u
#define IOC_READWRITE 3u

#define IOC_ENCODE(dir, type, nr, size)                                     \
    (((u64)(dir) << IOC_DIRSHIFT) | ((u64)(type) << IOC_TYPESHIFT) |         \
     ((u64)(nr) << 0) | ((u64)(size) << IOC_SIZESHIFT))

#define VIDIOC_QUERYCAP IOC_ENCODE(2u /* read */, 'V', 0, sizeof(struct RawV4l2Capability))
#define VIDIOC_ENUM_FMT IOC_ENCODE(IOC_READWRITE, 'V', 2, sizeof(struct RawV4l2Fmtdesc))
#define VIDIOC_G_FMT IOC_ENCODE(IOC_READWRITE, 'V', 4, sizeof(struct RawV4l2Format))
#define VIDIOC_S_FMT IOC_ENCODE(IOC_READWRITE, 'V', 5, sizeof(struct RawV4l2Format))
#define VIDIOC_REQBUFS IOC_ENCODE(IOC_READWRITE, 'V', 8, sizeof(struct RawV4l2RequestBuffers))
#define VIDIOC_QUERYBUF IOC_ENCODE(IOC_READWRITE, 'V', 9, sizeof(struct RawV4l2Buffer))
#define VIDIOC_QBUF IOC_ENCODE(IOC_READWRITE, 'V', 15, sizeof(struct RawV4l2Buffer))
#define VIDIOC_DQBUF IOC_ENCODE(IOC_READWRITE, 'V', 17, sizeof(struct RawV4l2Buffer))
#define VIDIOC_STREAMON IOC_ENCODE(IOC_WRITE, 'V', 18, sizeof(u32))
#define VIDIOC_STREAMOFF IOC_ENCODE(IOC_WRITE, 'V', 19, sizeof(u32))
#define VIDIOC_G_PARM IOC_ENCODE(IOC_READWRITE, 'V', 21, sizeof(struct RawV4l2StreamParm))
#define VIDIOC_S_PARM IOC_ENCODE(IOC_READWRITE, 'V', 22, sizeof(struct RawV4l2StreamParm))
#define VIDIOC_TRY_FMT IOC_ENCODE(IOC_READWRITE, 'V', 64, sizeof(struct RawV4l2Format))
#define VIDIOC_ENUM_FRAMESIZES                                               \
    IOC_ENCODE(IOC_READWRITE, 'V', 74, sizeof(struct RawV4l2Frmsizeenum))
#define VIDIOC_ENUM_FRAMEINTERVALS                                           \
    IOC_ENCODE(IOC_READWRITE, 'V', 75, sizeof(struct RawV4l2Frmivalenum))

struct MappedBuffer {
    u8 *data;
    usize length;
};

struct V4l2DeviceHandle {
    MemoryBlock block;
    i32 fd;
    bool configured;
    bool streaming;
    usize buffer_count;
    usize outstanding_count;
    MemoryBlock buffers_block; /* array of struct MappedBuffer, buffer_count long */
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

static i32 ioctl_ptr(i32 fd, u64 request, void *argument)
{
    return ioctl(fd, request, (unsigned long)(uintptr_t)argument);
}

/*
 * Zeroes an ioctl struct before it is filled in. Real V4L2 drivers document
 * every "reserved" field as "must be set to 0 by applications", and this
 * also keeps fields this library does not set explicitly (e.g. flag bits
 * QBUF/DQBUF read as input) from being uninitialised stack garbage.
 */
static void zero_struct(void *pointer, usize size)
{
    Bytes view;

    view.data = (u8 *)pointer;
    view.size = size;
    (void)memory_set(view, 0);
}

static struct MappedBuffer *buffers_of(struct V4l2DeviceHandle *device)
{
    return (struct MappedBuffer *)device->buffers_block.data;
}

/* Releases whatever MMAP buffers are currently mapped (if any) and forgets
 * them; used both by v4l2_stop_capture() and to replace buffers from an
 * earlier v4l2_configure_capture() before creating new ones. */
static ErrorCode release_buffers(struct V4l2DeviceHandle *device)
{
    ErrorCode result = CORE_OK;
    usize i;

    if (device->buffer_count == 0) {
        return CORE_OK;
    }
    for (i = 0; i < device->buffer_count; ++i) {
        struct MappedBuffer *buffer = &buffers_of(device)[i];

        /* A buffer whose mmap() never ran (v4l2_configure_capture() failed
         * partway through mapping) is still zero-filled: skip it, since
         * munmap(NULL, 0) itself fails with EINVAL. */
        if (buffer->data == NULL) {
            continue;
        }
        if (munmap(buffer->data, buffer->length) != 0 && result == CORE_OK) {
            result = last_error();
        }
    }
    {
        struct RawV4l2RequestBuffers reqbufs;

        zero_struct(&reqbufs, sizeof(reqbufs));
        reqbufs.count = 0;
        reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        reqbufs.memory = V4L2_MEMORY_MMAP;
        if (ioctl_ptr(device->fd, VIDIOC_REQBUFS, &reqbufs) != 0 && result == CORE_OK) {
            result = last_error();
        }
    }
    {
        MemoryBlock buffers_block = device->buffers_block;

        (void)memory_free(&buffers_block);
        device->buffers_block = buffers_block;
    }
    device->buffer_count = 0;
    device->configured = false;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Open / fd / close                                                       */
/* ------------------------------------------------------------------------ */

ErrorCode v4l2_open(const V4l2DeviceConfig *config, V4l2DeviceHandle *out_device)
{
    const char *path_data;
    i32 fd;
    MemoryBlock handle_block;
    struct V4l2DeviceHandle *device;
    ErrorCode result;

    if (config == NULL || out_device == NULL ||
        string_c_str(config->path, &path_data) != CORE_OK) {
        return CORE_INVALID_ARGUMENT;
    }

    fd = open(path_data, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
    if (fd < 0) {
        return last_error();
    }

    result = memory_allocate(sizeof(struct V4l2DeviceHandle), &handle_block);
    if (result != CORE_OK) {
        (void)close(fd);
        return result;
    }

    device = (struct V4l2DeviceHandle *)handle_block.data;
    device->block = handle_block;
    device->fd = fd;
    /* configured, streaming, buffer_count, outstanding_count and
     * buffers_block are already false/0/{NULL,0} from the zero-filled
     * allocation. */

    *out_device = device;
    return CORE_OK;
}

ErrorCode v4l2_get_fd(V4l2DeviceHandle device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = device->fd;
    return CORE_OK;
}

ErrorCode v4l2_close(V4l2DeviceHandle *device)
{
    struct V4l2DeviceHandle *handle;
    ErrorCode result = CORE_OK;
    MemoryBlock handle_block;

    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *device;
    if (handle->outstanding_count != 0) {
        return CORE_INVALID_STATE;
    }

    result = release_buffers(handle);
    if (close(handle->fd) != 0 && result == CORE_OK) {
        result = last_error();
    }
    handle_block = handle->block;
    (void)memory_free(&handle_block);
    *device = NULL;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Enumerating configurations                                               */
/* ------------------------------------------------------------------------ */

/* Appends one V4l2StreamConfig to *list, growing it (doubling) through
 * libmem as needed. */
static ErrorCode append_config(V4l2StreamConfigList *list, usize *capacity,
                               const V4l2StreamConfig *config)
{
    if (list->count == *capacity) {
        usize new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        usize new_size;
        MemoryBlock block;

        if (core_size_multiply(new_capacity, sizeof(V4l2StreamConfig), &new_size) != CORE_OK) {
            return CORE_OVERFLOW;
        }
        block.data = (u8 *)list->items;
        block.size = *capacity * sizeof(V4l2StreamConfig);
        {
            ErrorCode result = memory_reallocate(&block, new_size);

            if (result != CORE_OK) {
                return result;
            }
        }
        list->items = (V4l2StreamConfig *)block.data;
        *capacity = new_capacity;
    }
    list->items[list->count] = *config;
    ++list->count;
    return CORE_OK;
}

/* Queries sizeimage for (pixel_format, width, height) via VIDIOC_TRY_FMT,
 * which negotiates the format without changing the device's current one
 * (contract 12: "cannot be determined without starting capture"). */
static ErrorCode try_frame_bytes(i32 fd, V4l2PixelFormat pixel_format, u32 width, u32 height,
                                 u32 *out_frame_bytes)
{
    struct RawV4l2Format format;

    zero_struct(&format, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixel_format;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl_ptr(fd, VIDIOC_TRY_FMT, &format) != 0) {
        return last_error();
    }
    if (format.fmt.pix.sizeimage == 0) {
        return CORE_UNSUPPORTED;
    }
    *out_frame_bytes = format.fmt.pix.sizeimage;
    return CORE_OK;
}

ErrorCode v4l2_get_capture_configurations(V4l2DeviceHandle device,
                                          V4l2StreamConfigList *out_configurations)
{
    V4l2StreamConfigList list;
    usize capacity = 0;
    u32 format_index;
    ErrorCode result = CORE_OK;

    if (device == NULL || out_configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    list.items = NULL;
    list.count = 0;

    for (format_index = 0;; ++format_index) {
        struct RawV4l2Fmtdesc fmtdesc;
        u32 size_index;

        zero_struct(&fmtdesc, sizeof(fmtdesc));
        fmtdesc.index = format_index;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl_ptr(device->fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
            if (errno == EINVAL) {
                break; /* end of the format list */
            }
            result = last_error();
            goto fail;
        }

        for (size_index = 0;; ++size_index) {
            struct RawV4l2Frmsizeenum frmsize;
            u32 width;
            u32 height;
            u32 fps_index;

            zero_struct(&frmsize, sizeof(frmsize));
            frmsize.index = size_index;
            frmsize.pixel_format = fmtdesc.pixelformat;
            if (ioctl_ptr(device->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) != 0) {
                if (errno == EINVAL) {
                    break; /* end of the size list for this format */
                }
                result = last_error();
                goto fail;
            }
            if (frmsize.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                result = CORE_UNSUPPORTED;
                goto fail;
            }
            width = frmsize.size[0];
            height = frmsize.size[1];

            for (fps_index = 0;; ++fps_index) {
                struct RawV4l2Frmivalenum frmival;
                u32 numerator;
                u32 denominator;
                V4l2StreamConfig config;

                zero_struct(&frmival, sizeof(frmival));
                frmival.index = fps_index;
                frmival.pixel_format = fmtdesc.pixelformat;
                frmival.width = width;
                frmival.height = height;
                if (ioctl_ptr(device->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) != 0) {
                    if (errno == EINVAL) {
                        break; /* end of the frame-rate list for this size */
                    }
                    result = last_error();
                    goto fail;
                }
                if (frmival.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                    result = CORE_UNSUPPORTED;
                    goto fail;
                }
                /* interval[0..1] is v4l2_fract { numerator, denominator },
                 * the time per frame in seconds; frames per second is its
                 * reciprocal. */
                numerator = frmival.interval[0];
                denominator = frmival.interval[1];
                if (numerator == 0 || denominator == 0) {
                    result = CORE_UNSUPPORTED;
                    goto fail;
                }

                config.pixel_format = fmtdesc.pixelformat;
                config.width = width;
                config.height = height;
                config.frames_per_second = denominator / numerator;
                if (config.frames_per_second == 0) {
                    result = CORE_UNSUPPORTED;
                    goto fail;
                }
                result = try_frame_bytes(device->fd, fmtdesc.pixelformat, width, height,
                                         &config.max_frame_bytes);
                if (result != CORE_OK) {
                    goto fail;
                }

                result = append_config(&list, &capacity, &config);
                if (result != CORE_OK) {
                    goto fail;
                }
            }
        }
    }

    /* Trim the list to exactly list.count entries. */
    if (list.count != capacity) {
        MemoryBlock block;

        block.data = (u8 *)list.items;
        block.size = capacity * sizeof(V4l2StreamConfig);
        result = memory_reallocate(&block, list.count * sizeof(V4l2StreamConfig));
        if (result != CORE_OK) {
            goto fail;
        }
        list.items = (V4l2StreamConfig *)block.data;
    }
    *out_configurations = list;
    return CORE_OK;

fail:
    if (list.items != NULL) {
        MemoryBlock block;

        block.data = (u8 *)list.items;
        block.size = capacity * sizeof(V4l2StreamConfig);
        (void)memory_free(&block);
    }
    return result;
}

ErrorCode v4l2_stream_config_list_destroy(V4l2StreamConfigList *configurations)
{
    MemoryBlock block;

    if (configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    block.data = (u8 *)configurations->items;
    block.size = configurations->count * sizeof(V4l2StreamConfig);
    {
        ErrorCode result = memory_free(&block);

        if (result != CORE_OK) {
            return result;
        }
    }
    configurations->items = NULL;
    configurations->count = 0;
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Configuring and streaming                                                */
/* ------------------------------------------------------------------------ */

static bool stream_config_is_valid_request(const V4l2StreamConfig *config)
{
    return config->width >= V4L2_MIN_WIDTH && config->height >= V4L2_MIN_HEIGHT &&
           config->frames_per_second >= V4L2_MIN_FRAMES_PER_SECOND &&
           config->max_frame_bytes == 0;
}

ErrorCode v4l2_configure_capture(V4l2DeviceHandle device, const V4l2StreamConfig *requested_config,
                                 V4l2StreamConfig *out_actual_config)
{
    struct RawV4l2Format format;
    struct RawV4l2StreamParm streamparm;
    struct RawV4l2RequestBuffers reqbufs;
    ErrorCode result;
    usize i;

    if (device == NULL || requested_config == NULL || out_actual_config == NULL ||
        !stream_config_is_valid_request(requested_config)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->outstanding_count != 0) {
        return CORE_INVALID_STATE;
    }
    if (device->configured) {
        result = release_buffers(device);
        if (result != CORE_OK) {
            return result;
        }
    }

    zero_struct(&format, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = requested_config->width;
    format.fmt.pix.height = requested_config->height;
    format.fmt.pix.pixelformat = requested_config->pixel_format;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl_ptr(device->fd, VIDIOC_S_FMT, &format) != 0) {
        return last_error();
    }

    zero_struct(&streamparm, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe_numerator = 1;
    streamparm.parm.capture.timeperframe_denominator = requested_config->frames_per_second;
    if (ioctl_ptr(device->fd, VIDIOC_S_PARM, &streamparm) != 0) {
        return last_error();
    }

    zero_struct(&reqbufs, sizeof(reqbufs));
    reqbufs.count = V4L2_BUFFER_COUNT;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    if (ioctl_ptr(device->fd, VIDIOC_REQBUFS, &reqbufs) != 0) {
        return last_error();
    }
    if (reqbufs.count == 0) {
        return CORE_UNSUPPORTED;
    }

    result = memory_allocate(reqbufs.count * sizeof(struct MappedBuffer), &device->buffers_block);
    if (result != CORE_OK) {
        return result;
    }
    device->buffer_count = reqbufs.count;

    for (i = 0; i < device->buffer_count; ++i) {
        struct RawV4l2Buffer querybuf;
        void *mapping;

        zero_struct(&querybuf, sizeof(querybuf));
        querybuf.index = (u32)i;
        querybuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        querybuf.memory = V4L2_MEMORY_MMAP;
        if (ioctl_ptr(device->fd, VIDIOC_QUERYBUF, &querybuf) != 0) {
            result = last_error();
            goto fail_buffers;
        }

        mapping = mmap(NULL, querybuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd,
                       (off_t)(u32)querybuf.m);
        if (mapping == MAP_FAILED) {
            result = last_error();
            goto fail_buffers;
        }
        buffers_of(device)[i].data = (u8 *)mapping;
        buffers_of(device)[i].length = querybuf.length;
    }

    /* Read back the frame rate the driver actually applied: VIDIOC_S_PARM
     * may adjust a rate the hardware cannot produce exactly. */
    if (ioctl_ptr(device->fd, VIDIOC_G_PARM, &streamparm) == 0 &&
        streamparm.parm.capture.timeperframe_numerator != 0) {
        out_actual_config->frames_per_second = streamparm.parm.capture.timeperframe_denominator /
                                               streamparm.parm.capture.timeperframe_numerator;
    } else {
        out_actual_config->frames_per_second = requested_config->frames_per_second;
    }
    out_actual_config->pixel_format = format.fmt.pix.pixelformat;
    out_actual_config->width = format.fmt.pix.width;
    out_actual_config->height = format.fmt.pix.height;
    out_actual_config->max_frame_bytes = format.fmt.pix.sizeimage;
    device->configured = true;
    return CORE_OK;

fail_buffers:
    (void)release_buffers(device);
    return result;
}

ErrorCode v4l2_start_capture(V4l2DeviceHandle device)
{
    u32 type_value = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    usize i;

    if (device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!device->configured) {
        return CORE_INVALID_STATE;
    }

    for (i = 0; i < device->buffer_count; ++i) {
        struct RawV4l2Buffer buf;

        zero_struct(&buf, sizeof(buf));
        buf.index = (u32)i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl_ptr(device->fd, VIDIOC_QBUF, &buf) != 0) {
            return last_error();
        }
    }
    if (ioctl_ptr(device->fd, VIDIOC_STREAMON, &type_value) != 0) {
        return last_error();
    }
    device->streaming = true;
    return CORE_OK;
}

ErrorCode v4l2_dequeue_buffer(V4l2DeviceHandle device, V4l2DequeueStatus *out_status,
                              V4l2Buffer *out_buffer)
{
    struct RawV4l2Buffer buf;
    MonotonicTime now;
    ErrorCode result;

    if (device == NULL || out_status == NULL || out_buffer == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    zero_struct(&buf, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl_ptr(device->fd, VIDIOC_DQBUF, &buf) != 0) {
        if (errno == EAGAIN) {
            *out_status = V4L2_DEQUEUE_STATUS_NO_BUFFER;
            return CORE_OK;
        }
        if (errno == ENODEV) {
            *out_status = V4L2_DEQUEUE_STATUS_DEVICE_LOST;
            return CORE_OK;
        }
        return last_error();
    }

    result = time_monotonic_now(&now);
    if (result != CORE_OK) {
        return result;
    }
    out_buffer->index = buf.index;
    out_buffer->data.data = buffers_of(device)[buf.index].data;
    out_buffer->data.size = buf.bytesused;
    out_buffer->timestamp = now;
    out_buffer->sequence = buf.sequence;
    ++device->outstanding_count;
    *out_status = V4L2_DEQUEUE_STATUS_BUFFER_READY;
    return CORE_OK;
}

ErrorCode v4l2_queue_buffer(V4l2DeviceHandle device, V4l2Buffer *buffer)
{
    struct RawV4l2Buffer buf;

    if (device == NULL || buffer == NULL || buffer->index >= device->buffer_count) {
        return CORE_INVALID_ARGUMENT;
    }

    zero_struct(&buf, sizeof(buf));
    buf.index = buffer->index;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl_ptr(device->fd, VIDIOC_QBUF, &buf) != 0) {
        return last_error();
    }

    if (device->outstanding_count > 0) {
        --device->outstanding_count;
    }
    buffer->index = 0;
    buffer->data.data = NULL;
    buffer->data.size = 0;
    buffer->timestamp = 0;
    buffer->sequence = 0;
    return CORE_OK;
}

ErrorCode v4l2_stop_capture(V4l2DeviceHandle device)
{
    ErrorCode result = CORE_OK;

    if (device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->outstanding_count != 0) {
        return CORE_INVALID_STATE;
    }

    if (device->streaming) {
        u32 type_value = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl_ptr(device->fd, VIDIOC_STREAMOFF, &type_value) != 0) {
            result = last_error();
        }
        device->streaming = false;
    }
    {
        ErrorCode release_result = release_buffers(device);

        if (result == CORE_OK) {
            result = release_result;
        }
    }
    return result;
}
