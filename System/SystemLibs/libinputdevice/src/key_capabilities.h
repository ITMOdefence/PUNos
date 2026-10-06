#ifndef PUNOS_INPUT_KEY_CAPABILITIES_H
#define PUNOS_INPUT_KEY_CAPABILITIES_H

#include <sys/input-device.h>

/* Linux input-event-codes.h: KEY_* resume at 0x160 after the first BTN_*
 * range. D-pad/grip and trigger-happy buttons occupy two later ranges. */
static InputRawCapabilityMask input_raw_key_class(u32 code)
{
    if (code == 0 || code > 0x2ffu) { return 0; }
    if ((code >= 0x100u && code < 0x160u) ||
        (code >= 0x220u && code <= 0x227u) ||
        (code >= 0x2c0u && code <= 0x2e7u)) {
        return INPUT_RAW_CAP_POINTER_BUTTONS;
    }
    return INPUT_RAW_CAP_KEYBOARD_KEYS;
}

#endif
