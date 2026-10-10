/*
 * Overlay rendering: draws the panel into the swapchain image that is about
 * to be presented, using a tiny alpha-blended pipeline. All state is
 * created lazily per swapchain; any failure disables the overlay for that
 * swapchain only.
 */
#include "overlay.h"

#include <stdlib.h>
#include <string.h>

#include "fp_frag_spv.h"
#include "fp_vert_spv.h"
#include "panel.h"
#include "util.h"


struct ov_image {
    VkImageView view;
    VkFramebuffer fb;
    VkCommandBuffer cb;
    VkBuffer buf;
    VkDeviceMemory mem;
    fp_vertex *map;
};

struct fp_overlay {
    VkExtent2D extent;
    bool srgb;
    VkRenderPass rp;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkCommandPool pool;
    uint32_t nimages;
    struct ov_image *img;
    fp_panel_cache cache;
};

static bool is_srgb(VkFormat f)
{
    return f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_SRGB ||
           f == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

static bool pick_memory_type(const fp_device *dev, uint32_t type_bits, uint32_t *out)
{
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < dev->memprops.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (dev->memprops.memoryTypes[i].propertyFlags & want) == want) {
            *out = i;
            return true;
        }
    }
    return false;
}

void fp_overlay_destroy(fp_device *dev, struct fp_overlay *ov)
{
    if (!ov)
        return;
    const fp_device_table *t = &dev->t;
    VkDevice d = dev->handle;

    for (uint32_t i = 0; i < ov->nimages; i++) {
        struct ov_image *im = &ov->img[i];
        if (im->map)
            t->UnmapMemory(d, im->mem);
        if (im->buf)
            t->DestroyBuffer(d, im->buf, NULL);
        if (im->mem)
            t->FreeMemory(d, im->mem, NULL);
        if (im->fb)
            t->DestroyFramebuffer(d, im->fb, NULL);
        if (im->view)
            t->DestroyImageView(d, im->view, NULL);
    }
    if (ov->pool)   /* frees the command buffers too */
        t->DestroyCommandPool(d, ov->pool, NULL);
    if (ov->pipeline)
        t->DestroyPipeline(d, ov->pipeline, NULL);
    if (ov->layout)
        t->DestroyPipelineLayout(d, ov->layout, NULL);
    if (ov->rp)
        t->DestroyRenderPass(d, ov->rp, NULL);
    free(ov->img);
    free(ov);
}

static bool create_pipeline(fp_device *dev, struct fp_overlay *ov)
{
    const fp_device_table *t = &dev->t;
    VkDevice d = dev->handle;

    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = sizeof fp_vert_spv;
    smci.pCode = fp_vert_spv;
    if (t->CreateShaderModule(d, &smci, NULL, &vs) != VK_SUCCESS)
        return false;
    smci.codeSize = sizeof fp_frag_spv;
    smci.pCode = fp_frag_spv;
    if (t->CreateShaderModule(d, &smci, NULL, &fs) != VK_SUCCESS) {
        t->DestroyShaderModule(d, vs, NULL);
        return false;
    }

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    bool ok = t->CreatePipelineLayout(d, &plci, NULL, &ov->layout) == VK_SUCCESS;

    if (ok) {
        VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" },
        };
        VkVertexInputBindingDescription bind = { 0, sizeof(fp_vertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2] = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(fp_vertex, x) },
            { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(fp_vertex, r) },
        };
        VkPipelineVertexInputStateCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bind,
            .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs };
        VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
        VkPipelineViewportStateCreateInfo vp = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .scissorCount = 1 };
        VkPipelineRasterizationStateCreateInfo rs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
        VkPipelineMultisampleStateCreateInfo ms = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
        VkPipelineColorBlendAttachmentState cba = {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
        VkPipelineColorBlendStateCreateInfo cb = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &cba };
        VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo ds = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2, .pDynamicStates = dyn };
        VkGraphicsPipelineCreateInfo gpci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages, .pVertexInputState = &vi,
            .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs,
            .pMultisampleState = &ms, .pColorBlendState = &cb, .pDynamicState = &ds,
            .layout = ov->layout, .renderPass = ov->rp, .subpass = 0 };
        ok = t->CreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gpci, NULL, &ov->pipeline) ==
             VK_SUCCESS;
    }
    t->DestroyShaderModule(d, vs, NULL);
    t->DestroyShaderModule(d, fs, NULL);
    return ok;
}

static bool create_image_resources(fp_device *dev, struct fp_overlay *ov, const fp_swapchain *sc,
                                   uint32_t i, VkImage image)
{
    const fp_device_table *t = &dev->t;
    VkDevice d = dev->handle;
    struct ov_image *im = &ov->img[i];

    VkImageViewCreateInfo ivci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = sc->format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (t->CreateImageView(d, &ivci, NULL, &im->view) != VK_SUCCESS)
        return false;

    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = ov->rp,
        .attachmentCount = 1, .pAttachments = &im->view,
        .width = sc->extent.width, .height = sc->extent.height, .layers = 1 };
    if (t->CreateFramebuffer(d, &fbci, NULL, &im->fb) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ov->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    if (t->AllocateCommandBuffers(d, &cbai, &im->cb) != VK_SUCCESS)
        return false;
    if (dev->set_loader_data && dev->set_loader_data(d, im->cb) != VK_SUCCESS)
        return false;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)FP_MAX_VERTS * sizeof(fp_vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (t->CreateBuffer(d, &bci, NULL, &im->buf) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    t->GetBufferMemoryRequirements(d, im->buf, &req);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = req.size };
    if (!pick_memory_type(dev, req.memoryTypeBits, &mai.memoryTypeIndex))
        return false;
    if (t->AllocateMemory(d, &mai, NULL, &im->mem) != VK_SUCCESS)
        return false;
    if (t->BindBufferMemory(d, im->buf, im->mem, 0) != VK_SUCCESS)
        return false;
    void *p = NULL;
    if (t->MapMemory(d, im->mem, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS || !p)
        return false;
    im->map = p;
    return true;
}

struct fp_overlay *fp_overlay_create(fp_device *dev, const fp_swapchain *sc,
                                     uint32_t queue_family)
{
    const fp_device_table *t = &dev->t;
    VkDevice d = dev->handle;

    uint32_t n = 0;
    if (t->GetSwapchainImagesKHR(d, sc->handle, &n, NULL) != VK_SUCCESS || n == 0)
        return NULL;
    VkImage *images = calloc(n, sizeof *images);
    struct fp_overlay *ov = calloc(1, sizeof *ov);
    if (!images || !ov)
        goto fail;
    ov->img = calloc(n, sizeof *ov->img);
    if (!ov->img)
        goto fail;
    ov->nimages = n;
    ov->extent = sc->extent;
    ov->srgb = is_srgb(sc->format);
    if (t->GetSwapchainImagesKHR(d, sc->handle, &n, images) != VK_SUCCESS)
        goto fail;

    VkAttachmentDescription att = {
        .format = sc->format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 .colorAttachmentCount = 1, .pColorAttachments = &ref };
    VkSubpassDependency deps[2] = {
        { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 },
        { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, 0 },
    };
    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                    .attachmentCount = 1, .pAttachments = &att,
                                    .subpassCount = 1, .pSubpasses = &sub,
                                    .dependencyCount = 2, .pDependencies = deps };
    if (t->CreateRenderPass(d, &rpci, NULL, &ov->rp) != VK_SUCCESS)
        goto fail;
    if (!create_pipeline(dev, ov))
        goto fail;

    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family };
    if (t->CreateCommandPool(d, &cpci, NULL, &ov->pool) != VK_SUCCESS)
        goto fail;

    for (uint32_t i = 0; i < n; i++)
        if (!create_image_resources(dev, ov, sc, i, images[i]))
            goto fail;

    free(images);
    return ov;

fail:
    free(images);
    fp_overlay_destroy(dev, ov);
    return NULL;
}

bool fp_overlay_record(fp_device *dev, struct fp_overlay *ov, uint32_t idx,
                       VkCommandBuffer *out_cb, bool *broken)
{
    const fp_device_table *t = &dev->t;
    *broken = false;
    if (idx >= ov->nimages)
        return false;
    struct ov_image *im = &ov->img[idx];

    uint32_t nverts = fp_panel_build(im->map, FP_MAX_VERTS, ov->extent.width,
                                     ov->extent.height, ov->srgb, &ov->cache);
    if (nverts == 0)
        return false;

    if (t->ResetCommandBuffer(im->cb, 0) != VK_SUCCESS) {
        *broken = true;
        return false;
    }
    VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    if (t->BeginCommandBuffer(im->cb, &cbbi) != VK_SUCCESS) {
        *broken = true;
        return false;
    }
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = ov->rp,
        .framebuffer = im->fb, .renderArea = { { 0, 0 }, ov->extent } };
    t->CmdBeginRenderPass(im->cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    t->CmdBindPipeline(im->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ov->pipeline);
    VkViewport vp = { 0, 0, (float)ov->extent.width, (float)ov->extent.height, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, ov->extent };
    t->CmdSetViewport(im->cb, 0, 1, &vp);
    t->CmdSetScissor(im->cb, 0, 1, &sc);
    VkDeviceSize off = 0;
    t->CmdBindVertexBuffers(im->cb, 0, 1, &im->buf, &off);
    t->CmdDraw(im->cb, nverts, 1, 0, 0);
    t->CmdEndRenderPass(im->cb);
    if (t->EndCommandBuffer(im->cb) != VK_SUCCESS) {
        *broken = true;
        return false;
    }
    *out_cb = im->cb;
    return true;
}
