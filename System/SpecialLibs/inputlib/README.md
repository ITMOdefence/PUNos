# inputlib — special/input

Implements `DevDoc/SpecialLibs/inputlib.md`. Only `sys/input-device` accesses
evdev; the adapter validates `/dev/input/eventN`, normalizes events and retains
partial frames across reads. `SYN_DROPPED` or frame overflow yields exactly one
`SyncLost`, then discards input through the next `SYN_REPORT`. Completed frames
survive allocation failures and can be retried. Disconnection discards partial
input and returns `DeviceLost`.

Classes use the raw EV_KEY bitmap to distinguish KEY_* from mouse buttons,
plus relative/absolute capability bits. Reading the bitmap is an additive
operation in `sys/input-device`; existing types and enum values are unchanged.

`make check` pins the ABI and tests ordering, normalization, partial frames,
multiple frames, 256-event boundaries, loss recovery and disconnect using a
scripted raw adapter. It needs no physical input device. Real evdev opening is
covered by `libinputdevice`'s hardware-aware self-test.

Cross-compilation and emulated runtime verification passed. In a scratch copy,
changing Key normalization to Raw failed at check 12 as expected. See the parent
README for the verification environment and native Linux/QEMU limitations.
