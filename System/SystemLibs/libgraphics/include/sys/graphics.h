#ifndef PUNOS_SYS_GRAPHICS_H
#define PUNOS_SYS_GRAPHICS_H

#include <sys/core.h>

#define GRAPHICS_DAMAGE_MAX_RECTS ((usize)64)
#define GRAPHICS_PRESENT_QUEUE_DEPTH ((usize)2)
#define GRAPHICS_SOFTWARE_OUTPUT_ID ((u64)1)
#define GRAPHICS_ORIENTATION_NORMAL ((u32)0)
#define GRAPHICS_ORIENTATION_90 ((u32)1)
#define GRAPHICS_ORIENTATION_180 ((u32)2)
#define GRAPHICS_ORIENTATION_270 ((u32)3)

typedef struct GraphicsDeviceHandle *GraphicsDeviceHandle;
typedef struct GraphicsFrameHandle *GraphicsFrameHandle;

typedef u32 GraphicsPixelFormat;
#define GRAPHICS_PIXEL_XRGB8888 ((GraphicsPixelFormat)1)
#define GRAPHICS_PIXEL_ARGB8888 ((GraphicsPixelFormat)2)

typedef struct GraphicsMode {
    u32 width_px;
    u32 height_px;
    u32 refresh_millihz;
} GraphicsMode;

typedef struct GraphicsDeviceConfig {
    GraphicsMode initial_mode;
    bool enabled;
} GraphicsDeviceConfig;

typedef struct GraphicsOutputInfo {
    u64 output_id;
    bool connected;
    bool enabled;
    u16 reserved;
    u32 physical_width_mm;
    u32 physical_height_mm;
    u32 orientation;
    GraphicsMode current_mode;
} GraphicsOutputInfo;

typedef struct GraphicsFrameDescriptor {
    u32 width;
    u32 height;
    u32 stride;
    GraphicsPixelFormat format;
    u64 byte_size;
} GraphicsFrameDescriptor;

typedef struct GraphicsRect {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
} GraphicsRect;

typedef struct GraphicsDamage {
    const GraphicsRect *rects;
    usize count;
    bool full;
} GraphicsDamage;

typedef enum GraphicsEventStatus {
    GRAPHICS_EVENT_NONE = 0,
    GRAPHICS_EVENT_READY = 1
} GraphicsEventStatus;

typedef enum GraphicsEventType {
    GRAPHICS_EVENT_PRESENT_COMPLETE = 1,
    GRAPHICS_EVENT_PRESENT_FAILED = 2,
    GRAPHICS_EVENT_DEVICE_LOST = 3
} GraphicsEventType;

typedef struct GraphicsEvent {
    GraphicsEventType type;
    ErrorCode error;
    u64 output_id;
    u64 present_id;
} GraphicsEvent;

ErrorCode graphics_device_open(const GraphicsDeviceConfig *config,
                               GraphicsDeviceHandle *out_device);
ErrorCode graphics_device_get_fd(GraphicsDeviceHandle device, i32 *out_fd);
ErrorCode graphics_device_get_output(GraphicsDeviceHandle device,
                                     GraphicsOutputInfo *out_output);
ErrorCode graphics_output_configure(GraphicsDeviceHandle device,
                                    bool enabled, GraphicsMode mode,
                                    u32 orientation);
ErrorCode graphics_frame_import(GraphicsDeviceHandle device,
                                const GraphicsFrameDescriptor *descriptor,
                                i32 fd, GraphicsFrameHandle *out_frame);
ErrorCode graphics_output_present(GraphicsDeviceHandle device, u64 output_id,
                                  GraphicsFrameHandle frame,
                                  const GraphicsDamage *damage,
                                  u64 present_id);
ErrorCode graphics_device_next_event(GraphicsDeviceHandle device,
                                     GraphicsEventStatus *out_status,
                                     GraphicsEvent *out_event);
ErrorCode graphics_frame_destroy(GraphicsFrameHandle *frame);
ErrorCode graphics_device_close(GraphicsDeviceHandle *device);

#endif
