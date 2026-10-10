#include "panel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "state.h"
#include "util.h"

typedef struct builder {
    fp_vertex *v;
    uint32_t n, max;
    float inv_w, inv_h;
    fp_color_kind kind;
} builder;

static void quad(builder *b, float x, float y, float w, float h,
                 float r, float g, float bl, float a)
{
    if (b->n + 6 > b->max)
        return;
    float rgb[3] = { r, g, bl };
    fp_color_encode(b->kind, rgb);
    r = rgb[0];
    g = rgb[1];
    bl = rgb[2];
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

#define GRAPH_BARS 120
#define GRAPH_MAX_NS 33000000.0f
#define AVG_WINDOW_NS 500000000ull
#define TEXT_REFRESH_NS 250000000ull

static void text(builder *b, float x, float y, float u, const char *s,
                 float r, float g, float bl, float a)
{
    for (; *s; s++) {
        const uint8_t *gl = fp_glyph(*s);
        for (int col = 0; col < FP_GLYPH_W; col++)
            for (int row = 0; row < FP_GLYPH_H; row++)
                if (gl[col] & (1u << row))
                    quad(b, x + (float)col * u, y + (float)row * u, u, u, r, g, bl, a);
        x += (float)(FP_GLYPH_W + 1) * u;
    }
}

/* Mean frame time over the last ~0.5 s, from the shared ring. */
static bool recent_mean_ns(const struct fp_state *s, double *mean)
{
    uint64_t seq = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
    uint32_t head = __atomic_load_n(&s->head, __ATOMIC_ACQUIRE);
    uint64_t sum = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < FP_RING_SIZE && i < seq; i++) {
        uint32_t fn = s->ring[(head + FP_RING_SIZE - i) % FP_RING_SIZE].frame_ns;
        if (fn == 0)
            continue;
        sum += fn;
        n++;
        if (sum >= AVG_WINDOW_NS)
            break;
    }
    if (n == 0)
        return false;
    *mean = (double)sum / n;
    return true;
}

uint32_t fp_panel_build(fp_vertex *out, uint32_t max, uint32_t w, uint32_t h, fp_color_kind kind,
                        fp_panel_cache *cache)
{
    const struct fp_state *s = g_fp_state;
    if (!s || w == 0 || h == 0)
        return 0;
    builder b = { out, 0, max, 1.0f / (float)w, 1.0f / (float)h, kind };

    uint64_t now = fp_now_ns();
    int32_t cap = __atomic_load_n(&s->cap, __ATOMIC_RELAXED);
    uint32_t flags = __atomic_load_n(&s->flags, __ATOMIC_RELAXED);

    /* Text is refreshed ~4x/s so the numbers stay readable. */
    if (cache->last_ns == 0 || now - cache->last_ns >= TEXT_REFRESH_NS) {
        double mean;
        if (recent_mean_ns(s, &mean))
            snprintf(cache->line1, sizeof cache->line1, "%.0f FPS %.1f MS", 1e9 / mean,
                     mean / 1e6);
        else
            snprintf(cache->line1, sizeof cache->line1, "-- FPS -- MS");
        char capstr[16];
        if (cap > 0)
            snprintf(capstr, sizeof capstr, "%d", cap);
        else
            snprintf(capstr, sizeof capstr, "OFF");
        const char *mode = (flags & FP_FLAG_ADAPTIVE) ? " A" : (flags & FP_FLAG_MANUAL) ? " M" : "";
        snprintf(cache->line2, sizeof cache->line2, "CAP %s%s", capstr, mode);
        cache->last_ns = now;
    }

    /* Layout in units of u = one font pixel (about height/480 px). */
    float u = floorf((float)h / 480.0f + 0.5f);
    if (u < 1.0f)
        u = 1.0f;
    const float margin = 24.0f, pad = 3.0f * u;
    const float line_h = 9.0f * u, graph_h = 24.0f * u;
    const float pw = GRAPH_BARS * u + 2.0f * pad;
    const float ph = pad + 2.0f * line_h + graph_h + pad;
    const float x0 = margin, y0 = margin;

    quad(&b, x0, y0, pw, ph, 0.0f, 0.0f, 0.0f, 0.55f);

    const float tc = 0.8f;
    text(&b, x0 + pad, y0 + pad, u, cache->line1, tc, tc, tc, 1.0f);
    text(&b, x0 + pad, y0 + pad + line_h, u, cache->line2, tc, tc, tc, 1.0f);

    /* Frame-time graph: last 120 frames, 0-33 ms, newest on the right. */
    const float gx = x0 + pad, gy = y0 + pad + 2.0f * line_h;
    uint64_t seq = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
    uint32_t head = __atomic_load_n(&s->head, __ATOMIC_ACQUIRE);
    float bar_w = u > 1.0f ? u - 1.0f : 1.0f;
    for (uint32_t i = 0; i < GRAPH_BARS; i++) {
        uint32_t age = GRAPH_BARS - 1 - i;
        if (age >= seq)
            continue;
        float fn = (float)s->ring[(head + FP_RING_SIZE - age) % FP_RING_SIZE].frame_ns;
        if (fn <= 0.0f)
            continue;
        float bh = fn / GRAPH_MAX_NS;
        if (bh > 1.0f)
            bh = 1.0f;
        bh *= graph_h;
        if (bh < 1.0f)
            bh = 1.0f;
        quad(&b, gx + (float)i * u, gy + graph_h - bh, bar_w, bh, 0.55f, 0.8f, 0.55f, 0.9f);
    }
    if (cap > 0) {
        float interval = 1e9f / (float)cap;
        if (interval <= GRAPH_MAX_NS) {
            float ly = gy + graph_h - interval / GRAPH_MAX_NS * graph_h;
            float lt = u / 2.0f < 1.0f ? 1.0f : floorf(u / 2.0f);
            quad(&b, gx, ly - lt / 2.0f, GRAPH_BARS * u, lt, 1.0f, 0.4f, 0.35f, 0.9f);
        }
    }
    return b.n;
}
