#ifndef MSGSERV_WIRE_H
#define MSGSERV_WIRE_H

/*
 * Разбор и сборка кадров протокола v1. Числа little-endian, строки UTF-8 без
 * NUL. Читатель никогда не выходит за границу буфера: каждое чтение сначала
 * проверяет остаток, и после ошибки курсор помечен как испорченный, поэтому
 * цепочку чтений можно проверять один раз в конце.
 */

#include <sys/core.h>

typedef struct MsgReader {
    const u8 *data;
    usize size;
    usize offset;
    bool bad;
} MsgReader;

typedef struct MsgWriter {
    u8 *data;
    usize capacity;
    usize offset;
    bool bad;
} MsgWriter;

/* Поле переменной длины внутри кадра; память принадлежит кадру. */
typedef struct MsgField {
    const u8 *data;
    usize length;
} MsgField;

static inline MsgReader msg_reader(const u8 *data, usize size)
{
    MsgReader reader = { data, size, 0, false };

    return reader;
}

static inline u64 msg_read_fixed(MsgReader *reader, usize width)
{
    u64 value = 0;

    if (reader->bad || reader->size - reader->offset < width) {
        reader->bad = true;
        return 0;
    }
    for (usize i = 0; i < width; ++i) {
        value |= (u64)reader->data[reader->offset + i] << (i * 8);
    }
    reader->offset += width;
    return value;
}

static inline u16 msg_read_u16(MsgReader *reader) { return (u16)msg_read_fixed(reader, 2); }
static inline u32 msg_read_u32(MsgReader *reader) { return (u32)msg_read_fixed(reader, 4); }
static inline u64 msg_read_u64(MsgReader *reader) { return msg_read_fixed(reader, 8); }
static inline u8 msg_read_u8(MsgReader *reader) { return (u8)msg_read_fixed(reader, 1); }

/* True, если байты - корректный UTF-8 без NUL (правила libstr). */
static inline bool msg_utf8_valid(const u8 *data, usize size)
{
    usize index = 0;

    while (index < size) {
        u8 lead = data[index];
        usize extra;
        u32 code;

        if (lead == 0) {
            return false;
        }
        if (lead < 0x80) {
            index += 1;
            continue;
        }
        if (lead >= 0xc2 && lead <= 0xdf) {
            extra = 1;
            code = lead & 0x1fu;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            extra = 2;
            code = lead & 0x0fu;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            extra = 3;
            code = lead & 0x07u;
        } else {
            return false;
        }
        if (size - index <= extra) {
            return false;
        }
        for (usize i = 1; i <= extra; ++i) {
            u8 byte = data[index + i];

            if ((byte & 0xc0u) != 0x80u) {
                return false;
            }
            code = (code << 6) | (byte & 0x3fu);
        }
        if ((extra == 2 && code < 0x800) || (extra == 3 && code < 0x10000) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            return false; /* сверхдлинная форма, суррогат или вне диапазона */
        }
        index += extra + 1;
    }
    return true;
}

/*
 * Текстовое поле: u16 длины, затем байты. Пустое поле допустимо только если
 * min_length равен нулю; UTF-8 проверяется всегда.
 */
static inline MsgField msg_read_text(MsgReader *reader, usize min_length, usize max_length)
{
    MsgField field = { NULL, 0 };
    usize length = msg_read_u16(reader);

    if (reader->bad || length < min_length || length > max_length ||
        length > reader->size - reader->offset) {
        reader->bad = true;
        return field;
    }
    if (!msg_utf8_valid(reader->data + reader->offset, length)) {
        reader->bad = true;
        return field;
    }
    field.data = reader->data + reader->offset;
    field.length = length;
    reader->offset += length;
    return field;
}

/* Полезная нагрузка: u32 длины, затем произвольные байты. */
static inline MsgField msg_read_blob(MsgReader *reader, usize max_length)
{
    MsgField field = { NULL, 0 };
    usize length = msg_read_u32(reader);

    if (reader->bad || length > max_length || length > reader->size - reader->offset) {
        reader->bad = true;
        return field;
    }
    field.data = reader->data + reader->offset;
    field.length = length;
    reader->offset += length;
    return field;
}

/* Кадр разобран правильно только если чтения прошли и лишних байт не осталось. */
static inline bool msg_reader_done(const MsgReader *reader)
{
    return !reader->bad && reader->offset == reader->size;
}

static inline MsgWriter msg_writer(u8 *data, usize capacity)
{
    MsgWriter writer = { data, capacity, 0, false };

    return writer;
}

static inline void msg_write_fixed(MsgWriter *writer, usize width, u64 value)
{
    if (writer->bad || writer->capacity - writer->offset < width) {
        writer->bad = true;
        return;
    }
    for (usize i = 0; i < width; ++i) {
        writer->data[writer->offset + i] = (u8)(value >> (i * 8));
    }
    writer->offset += width;
}

static inline void msg_write_u8(MsgWriter *writer, u8 value) { msg_write_fixed(writer, 1, value); }
static inline void msg_write_u16(MsgWriter *writer, u16 value) { msg_write_fixed(writer, 2, value); }
static inline void msg_write_u32(MsgWriter *writer, u32 value) { msg_write_fixed(writer, 4, value); }
static inline void msg_write_u64(MsgWriter *writer, u64 value) { msg_write_fixed(writer, 8, value); }

static inline void msg_write_bytes(MsgWriter *writer, const u8 *data, usize size)
{
    if (writer->bad || writer->capacity - writer->offset < size) {
        writer->bad = true;
        return;
    }
    for (usize i = 0; i < size; ++i) {
        writer->data[writer->offset + i] = data[i];
    }
    writer->offset += size;
}

static inline void msg_write_text(MsgWriter *writer, const u8 *data, usize size)
{
    msg_write_u16(writer, (u16)size);
    msg_write_bytes(writer, data, size);
}

static inline void msg_write_blob(MsgWriter *writer, const u8 *data, usize size)
{
    msg_write_u32(writer, (u32)size);
    msg_write_bytes(writer, data, size);
}

/* Сравнение без ранних выходов: время не зависит от числа совпавших байт. */
static inline bool msg_bytes_equal_constant_time(const u8 *left, usize left_size,
                                                 const u8 *right, usize right_size)
{
    u8 difference = 0;

    if (left_size != right_size) {
        return false;
    }
    for (usize i = 0; i < left_size; ++i) {
        difference |= (u8)(left[i] ^ right[i]);
    }
    return difference == 0;
}

static inline bool msg_bytes_equal(const u8 *left, usize left_size, const u8 *right,
                                   usize right_size)
{
    if (left_size != right_size) {
        return false;
    }
    for (usize i = 0; i < left_size; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

#endif
