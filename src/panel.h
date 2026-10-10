#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "color.h"

/* Overlay vertex: NDC position + straight-alpha colour (matches the pipeline). */
typedef struct fp_vertex {
    float x, y;
    float r, g, b, a;
} fp_vertex;

#define FP_MAX_QUADS 6000
#define FP_MAX_VERTS (FP_MAX_QUADS * 6)

typedef struct fp_panel_cache {
    uint64_t last_ns;       /* when the text was last refreshed */
    char line1[40];
    char line2[40];
} fp_panel_cache;

/* Build the panel geometry for a w x h swapchain image. Returns the number
 * of vertices written (<= max). Colours are authored as sRGB and encoded for
 * the swapchain's colour space with `kind` (see color.h). */
uint32_t fp_panel_build(fp_vertex *out, uint32_t max, uint32_t w, uint32_t h, fp_color_kind kind,
                        fp_panel_cache *cache);
