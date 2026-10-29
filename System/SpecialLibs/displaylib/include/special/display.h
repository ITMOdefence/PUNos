#ifndef PUNOS_SPECIAL_DISPLAY_H
#define PUNOS_SPECIAL_DISPLAY_H

#include <sys/core.h>
#include <sys/string.h>

#define DISPLAY_DAMAGE_MAX_RECTS ((usize)64)
#define DISPLAY_PRESENT_QUEUE_DEPTH ((usize)2)

#define DISPLAY_ORIENTATION_NORMAL ((u32)0)
#define DISPLAY_ORIENTATION_90 ((u32)1)
#define DISPLAY_ORIENTATION_180 ((u32)2)
#define DISPLAY_ORIENTATION_270 ((u32)3)

typedef struct DisplayBackendHandle *DisplayBackendHandle;
typedef struct DisplayFrameHandle *DisplayFrameHandle;

typedef u32 DisplayPixelFormat;
#define DISPLAY_PIXEL_XRGB8888 ((DisplayPixelFormat)1)
#define DISPLAY_PIXEL_ARGB8888 ((DisplayPixelFormat)2)

typedef struct DisplayBackendConfig {
    const String *backend_name;
    const String *device_path;
} DisplayBackendConfig;

typedef struct DisplayMode {
    u32 width_px;
    u32 height_px;
    u32 refresh_millihz;
} DisplayMode;

typedef struct DisplayOutputInfo {
    u64 backend_output_id;
    String name;
    bool connected;
    bool enabled;
    u16 reserved;
    u32 physical_width_mm;
    u32 physical_height_mm;
    DisplayMode current_mode;
    DisplayMode *modes;
    usize mode_count;
} DisplayOutputInfo;

typedef struct DisplayOutputInfoList {
    DisplayOutputInfo *items;
    usize count;
} DisplayOutputInfoList;

typedef struct DisplayOutputConfig {
    bool enabled;
    u32 orientation;
    DisplayMode mode;
} DisplayOutputConfig;

typedef struct DisplayFrameDescriptor {
    u32 width;
    u32 height;
    u32 stride;
    DisplayPixelFormat format;
    u64 byte_size;
} DisplayFrameDescriptor;

typedef struct DisplayRect {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
} DisplayRect;

typedef struct DisplayDamage {
    const DisplayRect *rects;
    usize count;
    bool full;
} DisplayDamage;

typedef enum DisplayBackendEventType {
    DISPLAY_BACKEND_EVENT_OUTPUT_ADDED = 1,
    DISPLAY_BACKEND_EVENT_OUTPUT_REMOVED = 2,
    DISPLAY_BACKEND_EVENT_OUTPUT_CHANGED = 3,
    DISPLAY_BACKEND_EVENT_PRESENT_COMPLETE = 4,
    DISPLAY_BACKEND_EVENT_PRESENT_FAILED = 5,
    DISPLAY_BACKEND_EVENT_LOST = 6
} DisplayBackendEventType;

typedef struct DisplayBackendEvent {
    DisplayBackendEventType type;
    ErrorCode error;
    u64 backend_output_id;
    u64 present_id;
} DisplayBackendEvent;

typedef enum DisplayEventStatus {
    DISPLAY_EVENT_NO_EVENT = 0,
    DISPLAY_EVENT_READY = 1,
    DISPLAY_EVENT_BACKEND_LOST = 2
} DisplayEventStatus;

ErrorCode display_backend_open(const DisplayBackendConfig *config,
                               DisplayBackendHandle *out_backend);
ErrorCode display_backend_get_fd(DisplayBackendHandle backend, i32 *out_fd);
ErrorCode display_backend_list_outputs(DisplayBackendHandle backend,
                                       DisplayOutputInfoList *out_outputs);
ErrorCode display_output_info_list_destroy(DisplayOutputInfoList *outputs);
ErrorCode display_output_configure(DisplayBackendHandle backend, u64 output_id,
                                   const DisplayOutputConfig *config);
ErrorCode display_frame_import(DisplayBackendHandle backend,
                               const DisplayFrameDescriptor *descriptor,
                               i32 fd, DisplayFrameHandle *out_frame);
ErrorCode display_output_present(DisplayBackendHandle backend, u64 output_id,
                                 DisplayFrameHandle frame,
                                 const DisplayDamage *damage,
                                 u64 present_id);
ErrorCode display_backend_next_event(DisplayBackendHandle backend,
                                     DisplayEventStatus *out_status,
                                     DisplayBackendEvent *out_event);
ErrorCode display_frame_destroy(DisplayFrameHandle *frame);
ErrorCode display_backend_close(DisplayBackendHandle *backend);

#endif
