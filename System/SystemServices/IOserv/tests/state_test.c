#define CHECK_TEST_NAME "IOserv state"
#include <check.h>
#include "state.h"

static bool cleared(const IoDeviceState *state)
{
    if (state->touch_slot != 0) return false;
    for (usize i = 0; i < sizeof(state->pressed); ++i)
        if (state->pressed[i] != 0) return false;
    for (usize i = 0; i < 64; ++i)
        if (state->absolute[i] != 0) return false;
    for (usize i = 0; i < 32; ++i)
        if (state->touches[i]) return false;
    return true;
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
    io_state_apply(&first, &frame);
    frame.events = second_events;
    io_state_apply(&second, &frame);
    EXPECT(1, first.pressed[291] == 1 && first.touch_slot == 2 &&
              first.touches[2] && first.absolute[0x35] == 12345);
    EXPECT(2, second.pressed[30] == 1 && second.touch_slot == 1 &&
              second.touches[1] && second.absolute[0x36] == 444);

    InputEvent sync_lost = { INPUT_EVENT_KIND_SYNC_LOST, 0, 0 };
    frame.events = &sync_lost;
    frame.count = 1;
    io_state_apply(&first, &frame);
    EXPECT(3, cleared(&first));
    EXPECT(4, second.pressed[30] == 1 && second.touch_slot == 1 &&
              second.touches[1] && second.absolute[0x36] == 444);
    EXPECT(5, sync_lost.kind == INPUT_EVENT_KIND_SYNC_LOST && frame.count == 1);

    InputEvent next[] = {
        { INPUT_EVENT_KIND_KEY, 31, 1 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 }
    };
    frame.events = next;
    frame.count = 2;
    io_state_apply(&first, &frame);
    EXPECT(6, first.pressed[31] == 1 && first.pressed[291] == 0 &&
              first.touch_slot == 0 && !first.touches[2]);
    EXPECT(7, second.pressed[30] == 1 && second.touches[1]);
    check_passed();
    return 0;
}
