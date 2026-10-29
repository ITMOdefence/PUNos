#ifndef PUNOS_COMPOSITORSERV_RENDERER_H
#define PUNOS_COMPOSITORSERV_RENDERER_H

#include <gui/protocol.h>
#include <sys/core.h>

typedef struct CompositorRenderTarget {
    u8 *pixels;
    u32 width;
    u32 height;
    u32 stride;
} CompositorRenderTarget;

typedef struct CompositorRenderLayer {
    const u8 *pixels;
    u32 width;
    u32 height;
    u32 stride;
    GuiPixelFormat format;
    i32 x;
    i32 y;
    bool clip_set;
    i32 clip_x;
    i32 clip_y;
    u32 clip_width;
    u32 clip_height;
} CompositorRenderLayer;

ErrorCode compositor_render_clear(CompositorRenderTarget *target,
                                  u8 red, u8 green, u8 blue);
ErrorCode compositor_render_layer(CompositorRenderTarget *target,
                                  const CompositorRenderLayer *layer);
ErrorCode compositor_render_layers(CompositorRenderTarget *target,
                                   const CompositorRenderLayer *layers,
                                   usize layer_count);

#endif
