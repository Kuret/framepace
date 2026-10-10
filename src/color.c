#include "color.h"

#include <math.h>

static bool is_srgb_format(VkFormat f)
{
    return f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_SRGB ||
           f == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

fp_color_kind fp_color_kind_for(VkFormat format, VkColorSpaceKHR cs, bool *fallback)
{
    *fallback = false;
    switch (cs) {
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
        return is_srgb_format(format) ? FP_COLOR_SRGB_FORMAT : FP_COLOR_SRGB;
    case VK_COLOR_SPACE_HDR10_ST2084_EXT:
        return FP_COLOR_PQ;
    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
        return FP_COLOR_SCRGB;
    case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
        return FP_COLOR_BT2020_LINEAR;
    default:
        *fallback = true;     /* HLG, Dolby Vision, P3, ...: treat as sRGB */
        return is_srgb_format(format) ? FP_COLOR_SRGB_FORMAT : FP_COLOR_SRGB;
    }
}

const char *fp_color_kind_name(fp_color_kind kind)
{
    switch (kind) {
    case FP_COLOR_SRGB: return "sRGB";
    case FP_COLOR_SRGB_FORMAT: return "sRGB (hardware-encoded format)";
    case FP_COLOR_PQ: return "HDR10 PQ";
    case FP_COLOR_SCRGB: return "scRGB linear";
    case FP_COLOR_BT2020_LINEAR: return "BT.2020 linear";
    }
    return "?";
}

float fp_srgb_to_linear(float c)
{
    if (c <= 0.0f)
        return 0.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

float fp_pq_encode(float nits)
{
    const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    double y = nits / 10000.0;
    if (y < 0.0)
        y = 0.0;
    if (y > 1.0)
        y = 1.0;
    double p = pow(y, m1);
    return (float)pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

/* BT.709 -> BT.2020 primaries (linear light). */
static void to_bt2020(float c[3])
{
    float r = c[0], g = c[1], b = c[2];
    c[0] = 0.627404f * r + 0.329283f * g + 0.043313f * b;
    c[1] = 0.069097f * r + 0.919540f * g + 0.011362f * b;
    c[2] = 0.016391f * r + 0.088013f * g + 0.895595f * b;
}

void fp_color_encode(fp_color_kind kind, float rgb[3])
{
    if (kind == FP_COLOR_SRGB)
        return;
    for (int i = 0; i < 3; i++)
        rgb[i] = fp_srgb_to_linear(rgb[i]);
    const float scale = FP_PAPER_WHITE_NITS / FP_SCRGB_NITS_PER_UNIT;   /* 2.5 */
    switch (kind) {
    case FP_COLOR_SRGB_FORMAT:
        break;
    case FP_COLOR_SCRGB:
        for (int i = 0; i < 3; i++)
            rgb[i] *= scale;
        break;
    case FP_COLOR_BT2020_LINEAR:
        to_bt2020(rgb);
        for (int i = 0; i < 3; i++)
            rgb[i] *= scale;
        break;
    case FP_COLOR_PQ:
        to_bt2020(rgb);
        for (int i = 0; i < 3; i++)
            rgb[i] = fp_pq_encode(rgb[i] * FP_PAPER_WHITE_NITS);
        break;
    default:
        break;
    }
}
