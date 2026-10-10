#include "panel.h"

#include <math.h>

typedef struct builder {
    fp_vertex *v;
    uint32_t n, max;
    float inv_w, inv_h;
    bool srgb;
} builder;

static void quad(builder *b, float x, float y, float w, float h,
                 float r, float g, float bl, float a)
{
    if (b->n + 6 > b->max)
        return;
    if (b->srgb) {
        r = powf(r, 2.2f);
        g = powf(g, 2.2f);
        bl = powf(bl, 2.2f);
    }
    float x0 = x * b->inv_w * 2.0f - 1.0f, x1 = (x + w) * b->inv_w * 2.0f - 1.0f;
    float y0 = y * b->inv_h * 2.0f - 1.0f, y1 = (y + h) * b->inv_h * 2.0f - 1.0f;
    fp_vertex *v = b->v + b->n;
    v[0] = (fp_vertex){ x0, y0, r, g, bl, a };
    v[1] = (fp_vertex){ x1, y0, r, g, bl, a };
    v[2] = (fp_vertex){ x1, y1, r, g, bl, a };
    v[3] = v[0];
    v[4] = v[2];
    v[5] = (fp_vertex){ x0, y1, r, g, bl, a };
    b->n += 6;
}

uint32_t fp_panel_build(fp_vertex *out, uint32_t max, uint32_t w, uint32_t h, bool srgb,
                        fp_panel_cache *cache)
{
    (void)cache;
    builder b = { out, 0, max, 1.0f / (float)w, 1.0f / (float)h, srgb };
    quad(&b, 24, 24, 200, 80, 0.0f, 0.0f, 0.0f, 0.55f);
    return b.n;
}
