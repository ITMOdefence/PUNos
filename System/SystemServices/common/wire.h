#ifndef PUNOS_DEVICE_WIRE_H
#define PUNOS_DEVICE_WIRE_H
#include <sys/core.h>

static inline u64 wire_read(const u8 *data, usize size)
{
    u64 value = 0;
    for (usize i = 0; i < size; ++i) value |= (u64)data[i] << (i * 8);
    return value;
}

static inline void wire_write(u8 *data, usize size, u64 value)
{
    for (usize i = 0; i < size; ++i) data[i] = (u8)(value >> (i * 8));
}
#endif
