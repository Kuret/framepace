/* Known-value tests for the overlay colour encoding. */
#include <math.h>
#include <stdio.h>

#include "../src/color.h"

static int failures;

static void near(const char *what, float got, float want, float tol)
{
    if (fabsf(got - want) > tol) {
        printf("FAIL %s: got %.6f, want %.6f (+-%g)\n", what, got, want, tol);
        failures++;
    } else {
        printf("ok   %s = %.6f\n", what, got);
    }
}

static void same(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL %s: got %d, want %d\n", what, got, want);
        failures++;
    } else {
        printf("ok   %s = %d\n", what, got);
    }
}

int main(void)
{
    /* PQ inverse EOTF reference values */
    near("pq(0 nits)", fp_pq_encode(0.0f), 0.0f, 1e-5f);
    near("pq(100 nits)", fp_pq_encode(100.0f), 0.508f, 0.001f);
    near("pq(200 nits)", fp_pq_encode(200.0f), 0.580f, 0.001f);
    near("pq(1000 nits)", fp_pq_encode(1000.0f), 0.7518f, 0.001f);
    near("pq(10000 nits)", fp_pq_encode(10000.0f), 1.0f, 1e-5f);

    near("srgb->linear(0.5)", fp_srgb_to_linear(0.5f), 0.2140f, 0.0005f);
    near("srgb->linear(1.0)", fp_srgb_to_linear(1.0f), 1.0f, 1e-6f);
    near("srgb->linear(0.02)", fp_srgb_to_linear(0.02f), 0.02f / 12.92f, 1e-6f);

    float c[3] = { 1, 1, 1 };
    fp_color_encode(FP_COLOR_SRGB, c);
    near("srgb white r", c[0], 1.0f, 1e-6f);

    c[0] = c[1] = c[2] = 0.5f;
    fp_color_encode(FP_COLOR_SRGB, c);
    near("srgb passthrough 0.5", c[0], 0.5f, 1e-6f);

    c[0] = c[1] = c[2] = 0.5f;
    fp_color_encode(FP_COLOR_SRGB_FORMAT, c);
    near("srgb-format 0.5 (linear)", c[0], 0.2140f, 0.0005f);

    c[0] = c[1] = c[2] = 1.0f;
    fp_color_encode(FP_COLOR_SCRGB, c);
    near("scRGB white r", c[0], 2.5f, 1e-5f);
    near("scRGB white b", c[2], 2.5f, 1e-5f);

    c[0] = c[1] = c[2] = 0.5f;
    fp_color_encode(FP_COLOR_SCRGB, c);
    near("scRGB grey 0.5", c[1], 0.2140f * 2.5f, 0.001f);

    c[0] = c[1] = c[2] = 1.0f;
    fp_color_encode(FP_COLOR_BT2020_LINEAR, c);
    near("BT.2020 linear white r", c[0], 2.5f, 1e-4f);
    near("BT.2020 linear white g", c[1], 2.5f, 1e-4f);
    near("BT.2020 linear white b", c[2], 2.5f, 1e-4f);

    c[0] = 1; c[1] = 0; c[2] = 0;
    fp_color_encode(FP_COLOR_BT2020_LINEAR, c);
    near("BT.2020 linear red r", c[0], 0.627404f * 2.5f, 1e-4f);
    near("BT.2020 linear red g", c[1], 0.069097f * 2.5f, 1e-4f);
    near("BT.2020 linear red b", c[2], 0.016391f * 2.5f, 1e-4f);

    c[0] = c[1] = c[2] = 1.0f;
    fp_color_encode(FP_COLOR_PQ, c);
    near("PQ white (200 nits) r", c[0], 0.580f, 0.001f);
    near("PQ white (200 nits) g", c[1], 0.580f, 0.001f);
    near("PQ white (200 nits) b", c[2], 0.580f, 0.001f);

    c[0] = c[1] = c[2] = 0.0f;
    fp_color_encode(FP_COLOR_PQ, c);
    near("PQ black", c[0], 0.0f, 1e-5f);

    /* 0.8 "soft white" text: linear 0.6038 -> 120.8 nits */
    c[0] = c[1] = c[2] = 0.8f;
    fp_color_encode(FP_COLOR_PQ, c);
    near("PQ text 0.8", c[1], fp_pq_encode(200.0f * 0.6038f), 0.002f);

    bool fb;
    same("kind srgb unorm", fp_color_kind_for(VK_FORMAT_B8G8R8A8_UNORM,
                                              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, &fb), FP_COLOR_SRGB);
    same("kind srgb format", fp_color_kind_for(VK_FORMAT_B8G8R8A8_SRGB,
                                               VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, &fb),
         FP_COLOR_SRGB_FORMAT);
    same("kind pq", fp_color_kind_for(VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                      VK_COLOR_SPACE_HDR10_ST2084_EXT, &fb), FP_COLOR_PQ);
    same("pq fallback flag", fb, 0);
    same("kind scrgb", fp_color_kind_for(VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, &fb),
         FP_COLOR_SCRGB);
    same("kind bt2020", fp_color_kind_for(VK_FORMAT_R16G16B16A16_SFLOAT,
                                          VK_COLOR_SPACE_BT2020_LINEAR_EXT, &fb),
         FP_COLOR_BT2020_LINEAR);
    same("kind hlg", fp_color_kind_for(VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                       VK_COLOR_SPACE_HDR10_HLG_EXT, &fb), FP_COLOR_SRGB);
    same("hlg fallback flag", fb, 1);

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
