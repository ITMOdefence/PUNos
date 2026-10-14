#ifndef PUNOS_IOSERV_STATE_H
#define PUNOS_IOSERV_STATE_H

#include <special/input.h>

typedef struct IoDeviceState {
    u8 pressed[1024];
    i32 absolute[64];
    bool touches[32];
    u32 touch_slot;
} IoDeviceState;

void io_state_apply(IoDeviceState *state, const InputFrame *frame);

#endif
