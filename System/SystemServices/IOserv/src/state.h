#ifndef PUNOS_IOSERV_STATE_H
#define PUNOS_IOSERV_STATE_H

#include <special/input.h>
#include <sys/memory.h>

typedef struct IoTouch {
    i32 slot;
    i32 tracking_id;
} IoTouch;

typedef struct IoDeviceState {
    u8 pressed[1024];
    i32 absolute[64];
    MemoryBlock touches;
    usize touch_count;
    usize touch_capacity;
    i32 touch_slot;
} IoDeviceState;

ErrorCode io_state_apply(IoDeviceState *state, const InputFrame *frame);
ErrorCode io_state_clear(IoDeviceState *state);

#endif
