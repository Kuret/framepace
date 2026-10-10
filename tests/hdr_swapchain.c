/*
 * Manual test harness (needs a Wayland session): opens a small window, creates a
 * swapchain with the requested format and colour space, clears it and presents
 * N frames. Run it with the layer forced to check that overlay setup works for
 * HDR swapchains:
 *
 *   VK_ADD_IMPLICIT_LAYER_PATH=build/dev FRAMEPACE=1 FRAMEPACE_FORCE=1 FRAMEPACE_DEBUG=1 \
 *       build/hdr_swapchain hdr10|scrgb|bt2020|srgb [frames]
 *
 * Build it with `ninja -C build hdr_swapchain`. It exits 77 (skipped) when the
 * driver does not offer the requested format/colour-space pair.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor;
static struct xdg_wm_base *wm_base;
static int configured;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
    (void)d;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, v < 4 ? v : 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

static void ping(void *d, struct xdg_wm_base *b, uint32_t s) { (void)d; xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wm_listener = { ping };

static void surf_configure(void *d, struct xdg_surface *s, uint32_t serial)
{
    (void)d;
    xdg_surface_ack_configure(s, serial);
    configured = 1;
}
static const struct xdg_surface_listener surf_listener = { surf_configure };
static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static const struct xdg_toplevel_listener top_listener = { .configure = top_configure, .close = top_close };

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); return 1; } } while (0)

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "hdr10";
    int frames = argc > 2 ? atoi(argv[2]) : 120;
    VkColorSpaceKHR want_cs;
    VkFormat want_fmt;
    float clear[4] = { 0.05f, 0.1f, 0.3f, 1.0f };
    if (!strcmp(mode, "hdr10")) {
        want_cs = VK_COLOR_SPACE_HDR10_ST2084_EXT;
        want_fmt = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        clear[0] = clear[1] = 0.25f; clear[2] = 0.4f;    /* PQ code values */
    } else if (!strcmp(mode, "scrgb")) {
        want_cs = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        want_fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
        clear[0] = clear[1] = 0.02f; clear[2] = 0.08f;
    } else if (!strcmp(mode, "bt2020")) {
        want_cs = VK_COLOR_SPACE_BT2020_LINEAR_EXT;
        want_fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
        clear[0] = clear[1] = 0.02f; clear[2] = 0.08f;
    } else {
        want_cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        want_fmt = VK_FORMAT_B8G8R8A8_UNORM;
    }

    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "no Wayland display\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !wm_base) { fprintf(stderr, "missing wl_compositor/xdg_wm_base\n"); return 1; }
    xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
    struct wl_surface *ws = wl_compositor_create_surface(compositor);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm_base, ws);
    xdg_surface_add_listener(xs, &surf_listener, NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(top, &top_listener, NULL);
    xdg_toplevel_set_title(top, "framepace hdr test");
    wl_surface_commit(ws);
    while (!configured && wl_display_dispatch(dpy) >= 0)
        ;

    const char *iext[] = { "VK_KHR_surface", "VK_KHR_wayland_surface", "VK_EXT_swapchain_colorspace" };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .enabledExtensionCount = 3, .ppEnabledExtensionNames = iext };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    VkWaylandSurfaceCreateInfoKHR wsci = { .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                                           .display = dpy, .surface = ws };
    VkSurfaceKHR surf;
    CHECK(vkCreateWaylandSurfaceKHR(inst, &wsci, NULL, &surf));

    uint32_t n = 0;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, NULL));
    VkPhysicalDevice pds[8];
    if (n > 8) n = 8;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, pds));
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (uint32_t i = 0; i < n && !pd; i++) {      /* first device with a graphics+present queue */
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, NULL);
        VkQueueFamilyProperties qp[16];
        if (nq > 16) nq = 16;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, qp);
        for (uint32_t q = 0; q < nq; q++) {
            VkBool32 ok = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], q, surf, &ok);
            if (ok && (qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { pd = pds[i]; family = q; break; }
        }
    }
    if (!pd) { fprintf(stderr, "no usable device\n"); return 1; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);

    uint32_t nf = 0;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, NULL));
    VkSurfaceFormatKHR *fm = calloc(nf, sizeof *fm);
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, fm));
    int found = 0;
    for (uint32_t i = 0; i < nf; i++)
        if (fm[i].format == want_fmt && fm[i].colorSpace == want_cs)
            found = 1;
    if (!found) {
        printf("%s: %s offers no format %d with colour space %d: skipped\n", mode,
               props.deviceName, (int)want_fmt, (int)want_cs);
        return 77;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo dq = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                   .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &prio };
    const char *dext[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &dq, .enabledExtensionCount = 1,
                               .ppEnabledExtensionNames = dext };
    VkDevice dev;
    CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, family, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps));
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xffffffffu) { ext.width = 640; ext.height = 480; }
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surf, .minImageCount = count,
        .imageFormat = want_fmt, .imageColorSpace = want_cs, .imageExtent = ext, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
    VkSwapchainKHR sc;
    CHECK(vkCreateSwapchainKHR(dev, &sci, NULL, &sc));
    uint32_t ni = 0;
    CHECK(vkGetSwapchainImagesKHR(dev, sc, &ni, NULL));
    VkImage *imgs = calloc(ni, sizeof *imgs);
    CHECK(vkGetSwapchainImagesKHR(dev, sc, &ni, imgs));

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                    .queueFamilyIndex = family };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                        .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHECK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkSemaphoreCreateInfo semi = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkSemaphore acquire, render;
    VkFence fence;
    CHECK(vkCreateSemaphore(dev, &semi, NULL, &acquire));
    CHECK(vkCreateSemaphore(dev, &semi, NULL, &render));
    CHECK(vkCreateFence(dev, &fi, NULL, &fence));

    printf("%s: %s, format %d colour space %d, %ux%u, %u images, %d frames\n", mode,
           props.deviceName, (int)want_fmt, (int)want_cs, ext.width, ext.height, ni, frames);
    for (int f = 0; f < frames; f++) {
        uint32_t idx;
        CHECK(vkAcquireNextImageKHR(dev, sc, UINT64_MAX, acquire, VK_NULL_HANDLE, &idx));
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        CHECK(vkResetCommandBuffer(cb, 0));
        CHECK(vkBeginCommandBuffer(cb, &bi));
        VkImageSubresourceRange rng = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageMemoryBarrier b1 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = imgs[idx], .subresourceRange = rng };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, NULL, 0, NULL, 1, &b1);
        VkClearColorValue cv;
        memcpy(cv.float32, clear, sizeof clear);
        vkCmdClearColorImage(cb, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &rng);
        VkImageMemoryBarrier b2 = b1;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b2.dstAccessMask = 0;
        b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                             0, NULL, 0, NULL, 1, &b2);
        CHECK(vkEndCommandBuffer(cb));
        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
                            .pWaitSemaphores = &acquire, .pWaitDstStageMask = &wait,
                            .commandBufferCount = 1, .pCommandBuffers = &cb,
                            .signalSemaphoreCount = 1, .pSignalSemaphores = &render };
        CHECK(vkQueueSubmit(queue, 1, &si, fence));
        VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &render, .swapchainCount = 1, .pSwapchains = &sc,
                                .pImageIndices = &idx };
        CHECK(vkQueuePresentKHR(queue, &pi));
        CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
        CHECK(vkResetFences(dev, 1, &fence));
        wl_display_dispatch_pending(dpy);
    }
    CHECK(vkDeviceWaitIdle(dev));
    vkDestroyFence(dev, fence, NULL);
    vkDestroySemaphore(dev, render, NULL);
    vkDestroySemaphore(dev, acquire, NULL);
    vkDestroyCommandPool(dev, pool, NULL);
    vkDestroySwapchainKHR(dev, sc, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroySurfaceKHR(inst, surf, NULL);
    vkDestroyInstance(inst, NULL);
    printf("%s: presented %d frames\n", mode, frames);
    return 0;
}
