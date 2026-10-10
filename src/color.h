#pragma once
/*
 * Overlay colour encoding. The panel's colours are written as sRGB/BT.709
 * values; they are converted once per vertex on the CPU into whatever the
 * swapchain expects, so the overlay looks the same on SDR and HDR outputs.
 * Alpha is left alone (it stays linear coverage).
 */
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

#define FP_PAPER_WHITE_NITS 200.0f   /* sRGB 1.0 maps to this on HDR outputs */
#define FP_SCRGB_NITS_PER_UNIT 80.0f /* scRGB / BT.2020 linear: 1.0 = 80 nits */

typedef enum fp_color_kind {
    FP_COLOR_SRGB,        /* sRGB-encoded UNORM values: used as they are */
    FP_COLOR_SRGB_FORMAT, /* *_SRGB format: hardware encodes, so give it linear values */
    FP_COLOR_PQ,          /* HDR10_ST2084: BT.2020 primaries, PQ transfer, 200 nits paper white */
    FP_COLOR_SCRGB,       /* EXTENDED_SRGB_LINEAR: linear BT.709, 1.0 = 80 nits */
    FP_COLOR_BT2020_LINEAR, /* BT2020_LINEAR: linear BT.2020, 1.0 = 80 nits */
} fp_color_kind;

/* Pick the encoding for a swapchain. *fallback is set when the colour space is
 * not handled (it is then treated as sRGB). */
fp_color_kind fp_color_kind_for(VkFormat format, VkColorSpaceKHR colorspace, bool *fallback);
const char *fp_color_kind_name(fp_color_kind kind);

/* Encode an sRGB colour in place. */
void fp_color_encode(fp_color_kind kind, float rgb[3]);

/* Building blocks (exported for tests). */
float fp_srgb_to_linear(float c);
float fp_pq_encode(float nits);   /* absolute luminance in nits -> PQ code value 0..1 */
