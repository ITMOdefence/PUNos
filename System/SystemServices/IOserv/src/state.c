#include "state.h"

void io_state_apply(IoDeviceState *state, const InputFrame *frame)
{
    if (frame->count == 1 && frame->events[0].kind == INPUT_EVENT_KIND_SYNC_LOST) {
        for (usize i = 0; i < sizeof(state->pressed); ++i) state->pressed[i] = 0;
        for (usize i = 0; i < 64; ++i) state->absolute[i] = 0;
        for (usize i = 0; i < 32; ++i) state->touches[i] = false;
        state->touch_slot = 0;
        return;
    }
    for (usize i = 0; i < frame->count; ++i) {
        const InputEvent *event = &frame->events[i];
        if (event->kind == INPUT_EVENT_KIND_KEY && event->code < sizeof(state->pressed))
            state->pressed[event->code] = event->value != 0;
        else if (event->kind == INPUT_EVENT_KIND_ABSOLUTE_AXIS && event->code < 64) {
            state->absolute[event->code] = event->value;
            if (event->code == 0x2f && event->value >= 0 && event->value < 32)
                state->touch_slot = (u32)event->value;
            if (event->code == 0x39)
                state->touches[state->touch_slot] = event->value >= 0;
        }
    }
}
