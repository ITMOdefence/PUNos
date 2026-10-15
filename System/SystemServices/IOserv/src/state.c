#include "state.h"

ErrorCode io_state_clear(IoDeviceState *state)
{
    if (!state) return CORE_INVALID_ARGUMENT;
    ErrorCode error = memory_free(&state->touches);
    if (error != CORE_OK) return error;
    state->touch_count = 0;
    state->touch_capacity = 0;
    state->touch_slot = 0;
    for (usize i = 0; i < sizeof(state->pressed); ++i) state->pressed[i] = 0;
    for (usize i = 0; i < 64; ++i) state->absolute[i] = 0;
    return CORE_OK;
}

static ErrorCode update_touch(IoDeviceState *state, i32 tracking_id)
{
    IoTouch *touches = (IoTouch *)state->touches.data;
    usize at = 0;
    while (at < state->touch_count && touches[at].slot != state->touch_slot) ++at;
    if (tracking_id < 0) {
        if (at < state->touch_count) {
            for (usize i = at + 1; i < state->touch_count; ++i) touches[i - 1] = touches[i];
            --state->touch_count;
        }
        return CORE_OK;
    }
    if (at < state->touch_count) {
        touches[at].tracking_id = tracking_id;
        return CORE_OK;
    }
    if (state->touch_count == state->touch_capacity) {
        usize capacity = state->touch_capacity ? state->touch_capacity * 2 : 8;
        if (capacity < state->touch_capacity || capacity > (usize)-1 / sizeof(IoTouch))
            return CORE_OVERFLOW;
        ErrorCode error = memory_reallocate(&state->touches, capacity * sizeof(IoTouch));
        if (error != CORE_OK) return error;
        state->touch_capacity = capacity;
        touches = (IoTouch *)state->touches.data;
    }
    touches[state->touch_count++] = (IoTouch){ state->touch_slot, tracking_id };
    return CORE_OK;
}

ErrorCode io_state_apply(IoDeviceState *state, const InputFrame *frame)
{
    if (!state || !frame || !frame->events || !frame->count) return CORE_INVALID_ARGUMENT;
    if (frame->count == 1 && frame->events[0].kind == INPUT_EVENT_KIND_SYNC_LOST)
        return io_state_clear(state);
    for (usize i = 0; i < frame->count; ++i) {
        const InputEvent *event = &frame->events[i];
        if (event->kind == INPUT_EVENT_KIND_KEY && event->code < sizeof(state->pressed))
            state->pressed[event->code] = event->value != 0;
        else if (event->kind == INPUT_EVENT_KIND_ABSOLUTE_AXIS && event->code < 64) {
            state->absolute[event->code] = event->value;
            if (event->code == 0x2f) state->touch_slot = event->value;
            if (event->code == 0x39 && state->touch_slot >= 0) {
                ErrorCode error = update_touch(state, event->value);
                if (error != CORE_OK) return error;
            }
        }
    }
    return CORE_OK;
}
