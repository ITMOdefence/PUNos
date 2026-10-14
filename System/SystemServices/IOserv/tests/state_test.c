#define CHECK_TEST_NAME "IOserv state"
#include <check.h>
#include "state.h"

static bool cleared(const IoDeviceState *state)
{
    if (state->touch_slot != 0 ||
        state->touch_count || state->touch_capacity || state->touches.data) return false;
    for (usize i = 0; i < sizeof(state->pressed); ++i)
        if (state->pressed[i] != 0) return false;
    for (usize i = 0; i < 64; ++i)
        if (state->absolute[i] != 0) return false;
    return true;
}

static i32 tracking(const IoDeviceState *state, i32 slot)
{
    const IoTouch *touches = (const IoTouch *)state->touches.data;
    for (usize i = 0; i < state->touch_count; ++i)
        if (touches[i].slot == slot) return touches[i].tracking_id;
    return -1;
}

int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    IoDeviceState first = { 0 }, second = { 0 };
    InputEvent first_events[] = {
        { INPUT_EVENT_KIND_KEY, 291, 1 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 2 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 9 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x35, 12345 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    InputEvent second_events[] = {
        { INPUT_EVENT_KIND_KEY, 30, 1 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 1 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 3 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x36, 444 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    InputFrame frame = { first_events, 5 };
    EXPECT(1, io_state_apply(&first, &frame) == CORE_OK);
    frame.events = second_events;
    EXPECT(2, io_state_apply(&second, &frame) == CORE_OK);
    EXPECT(3, first.pressed[291] == 1 && first.touch_slot == 2 &&
              tracking(&first, 2) == 9 && first.absolute[0x35] == 12345);
    EXPECT(4, second.pressed[30] == 1 && second.touch_slot == 1 &&
              tracking(&second, 1) == 3 && second.absolute[0x36] == 444);

    InputEvent boundary[] = {
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 32 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 10 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 65536 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 11 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 2147483647 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 12 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, -1 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 13 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    frame.events = boundary;
    frame.count = sizeof(boundary) / sizeof(boundary[0]);
    EXPECT(5, io_state_apply(&first, &frame) == CORE_OK);
    EXPECT(6, first.touch_count == 4 && tracking(&first, 0) == -1 &&
              tracking(&first, 2) == 9 && tracking(&first, 32) == 10 &&
              tracking(&first, 65536) == 11 && tracking(&first, 2147483647) == 12 &&
              first.touch_slot == -1);
    InputEvent release[] = {
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 32 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, -1 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    frame.events = release;
    frame.count = 3;
    EXPECT(7, io_state_apply(&first, &frame) == CORE_OK);
    EXPECT(8, first.touch_count == 3 && tracking(&first, 32) == -1 &&
              tracking(&first, 65536) == 11 && tracking(&first, 0) == -1);
    InputEvent many[21];
    for (i32 i = 0; i < 10; ++i) {
        many[2 * i] = (InputEvent){ INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 100 + i };
        many[2 * i + 1] = (InputEvent){ INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 1000 + i };
    }
    many[20] = (InputEvent){ INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 };
    frame.events = many;
    frame.count = 21;
    EXPECT(9, io_state_apply(&first, &frame) == CORE_OK);
    EXPECT(10, first.touch_count == 13 && tracking(&first, 100) == 1000 &&
               tracking(&first, 109) == 1009 && tracking(&first, 65536) == 11);

    InputEvent sync_lost = { INPUT_EVENT_KIND_SYNC_LOST, 0, 0 };
    frame.events = &sync_lost;
    frame.count = 1;
    EXPECT(11, io_state_apply(&first, &frame) == CORE_OK && cleared(&first));
    EXPECT(12, second.pressed[30] == 1 && second.touch_slot == 1 &&
               tracking(&second, 1) == 3 && second.absolute[0x36] == 444);
    EXPECT(13, sync_lost.kind == INPUT_EVENT_KIND_SYNC_LOST && frame.count == 1);

    InputEvent after_sync[] = {
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x2f, 32 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 14 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    frame.events = after_sync;
    frame.count = 3;
    EXPECT(14, io_state_apply(&first, &frame) == CORE_OK);
    EXPECT(15, tracking(&first, 32) == 14 && tracking(&first, 0) == -1);
    frame.events = &sync_lost;
    frame.count = 1;
    EXPECT(16, io_state_apply(&first, &frame) == CORE_OK && cleared(&first));

    InputEvent default_slot[] = {
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x39, 16 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    frame.events = default_slot;
    frame.count = 2;
    EXPECT(17, io_state_apply(&first, &frame) == CORE_OK && tracking(&first, 0) == 16);
    EXPECT(18, io_state_clear(&first) == CORE_OK && cleared(&first));

    InputEvent next[] = {
        { INPUT_EVENT_KIND_KEY, 31, 1 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    frame.events = next;
    frame.count = 2;
    EXPECT(19, io_state_apply(&first, &frame) == CORE_OK);
    EXPECT(20, first.pressed[31] == 1 && first.pressed[291] == 0 &&
               first.touch_count == 0 && tracking(&first, 2) == -1);
    EXPECT(21, second.pressed[30] == 1 && tracking(&second, 1) == 3);
    EXPECT(22, io_state_clear(&first) == CORE_OK && io_state_clear(&second) == CORE_OK);
    check_passed();
    return 0;
}
