#include "renderer.h"

#define BYTES_PER_PIXEL ((u32)4)

static u8 blend_channel(u8 source, u8 destination, u8 alpha)
{
    u32 inverse = (u32)255 - alpha;
    u32 value = (u32)source + (((u32)destination * inverse + 127) / 255);
    return (u8)(value > 255 ? 255 : value);
}

static ErrorCode validate_target(const CompositorRenderTarget *target)
{
    u64 minimum_stride;

    if (target == NULL || target->pixels == NULL ||
        target->width == 0 || target->height == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    minimum_stride = (u64)target->width * BYTES_PER_PIXEL;
    if ((u64)target->stride < minimum_stride) return CORE_INVALID_ARGUMENT;
    return CORE_OK;
}

static ErrorCode validate_layer(const CompositorRenderLayer *layer)
{
    u64 minimum_stride;

    if (layer == NULL || layer->pixels == NULL ||
        layer->width == 0 || layer->height == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (layer->format != GUI_PIXEL_XRGB8888 &&
        layer->format != GUI_PIXEL_ARGB8888) {
        return CORE_UNSUPPORTED;
    }
    minimum_stride = (u64)layer->width * BYTES_PER_PIXEL;
    if ((u64)layer->stride < minimum_stride) return CORE_INVALID_ARGUMENT;
    if (layer->clip_set &&
        (layer->clip_width == 0 || layer->clip_height == 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode compositor_render_clear(CompositorRenderTarget *target,
                                  u8 red, u8 green, u8 blue)
{
    u32 y;
    ErrorCode result = validate_target(target);

    if (result != CORE_OK) return result;
    for (y = 0; y < target->height; ++y) {
        u32 x;
        u8 *row = target->pixels + (usize)y * target->stride;
        for (x = 0; x < target->width; ++x) {
            u8 *pixel = row + (usize)x * BYTES_PER_PIXEL;
            pixel[0] = blue;
            pixel[1] = green;
            pixel[2] = red;
            pixel[3] = 255;
        }
    }
    return CORE_OK;
}

ErrorCode compositor_render_layer(CompositorRenderTarget *target,
                                  const CompositorRenderLayer *layer)
{
    i64 source_left = 0;
    i64 source_top = 0;
    i64 source_right;
    i64 source_bottom;
    i64 target_left;
    i64 target_top;
    i64 target_right;
    i64 target_bottom;
    i64 y;
    ErrorCode result = validate_target(target);

    if (result != CORE_OK) return result;
    result = validate_layer(layer);
    if (result != CORE_OK) return result;

    source_right = layer->width;
    source_bottom = layer->height;
    if (layer->clip_set) {
        i64 clip_left = layer->clip_x;
        i64 clip_top = layer->clip_y;
        i64 clip_right = clip_left + (i64)layer->clip_width;
        i64 clip_bottom = clip_top + (i64)layer->clip_height;
        if (clip_left > source_left) source_left = clip_left;
        if (clip_top > source_top) source_top = clip_top;
        if (clip_right < source_right) source_right = clip_right;
        if (clip_bottom < source_bottom) source_bottom = clip_bottom;
    }

    if (source_left < 0) source_left = 0;
    if (source_top < 0) source_top = 0;
    if (source_right > (i64)layer->width) source_right = layer->width;
    if (source_bottom > (i64)layer->height) source_bottom = layer->height;
    if (source_left >= source_right || source_top >= source_bottom) return CORE_OK;

    target_left = (i64)layer->x + source_left;
    target_top = (i64)layer->y + source_top;
    target_right = (i64)layer->x + source_right;
    target_bottom = (i64)layer->y + source_bottom;

    if (target_left < 0) {
        source_left += -target_left;
        target_left = 0;
    }
    if (target_top < 0) {
        source_top += -target_top;
        target_top = 0;
    }
    if (target_right > (i64)target->width) {
        source_right -= target_right - (i64)target->width;
        target_right = target->width;
    }
    if (target_bottom > (i64)target->height) {
        source_bottom -= target_bottom - (i64)target->height;
        target_bottom = target->height;
    }
    if (source_left >= source_right || source_top >= source_bottom) return CORE_OK;

    for (y = source_top; y < source_bottom; ++y) {
        i64 x;
        i64 destination_y = (i64)layer->y + y;
        const u8 *source_row = layer->pixels + (usize)y * layer->stride;
        u8 *destination_row = target->pixels +
                              (usize)destination_y * target->stride;
        for (x = source_left; x < source_right; ++x) {
            i64 destination_x = (i64)layer->x + x;
            const u8 *source = source_row + (usize)x * BYTES_PER_PIXEL;
            u8 *destination = destination_row +
                              (usize)destination_x * BYTES_PER_PIXEL;

            if (layer->format == GUI_PIXEL_XRGB8888) {
                destination[0] = source[0];
                destination[1] = source[1];
                destination[2] = source[2];
                destination[3] = 255;
            } else {
                u8 alpha = source[3];
                destination[0] = blend_channel(source[0], destination[0], alpha);
                destination[1] = blend_channel(source[1], destination[1], alpha);
                destination[2] = blend_channel(source[2], destination[2], alpha);
                destination[3] = 255;
            }
        }
    }
    return CORE_OK;
}

ErrorCode compositor_render_layers(CompositorRenderTarget *target,
                                   const CompositorRenderLayer *layers,
                                   usize layer_count)
{
    usize index;
    ErrorCode result;

    if (layer_count != 0 && layers == NULL) return CORE_INVALID_ARGUMENT;
    result = validate_target(target);
    if (result != CORE_OK) return result;
    for (index = 0; index < layer_count; ++index) {
        result = compositor_render_layer(target, &layers[index]);
        if (result != CORE_OK) return result;
    }
    return CORE_OK;
}
