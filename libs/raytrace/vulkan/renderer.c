#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef RT_HAVE_VULKAN_BACKEND

/* Vulkan compute backend — Stage 1.
 *
 * Pipeline + descriptor sets + dispatch are real. SSBO uploads are not
 * — every primitive count in the UBO is set to 0 and the SSBO bindings
 * point at a single shared 64-byte zero buffer. The shader still runs
 * but every ray misses, producing a black frame. Camera and viewport
 * are honoured, so changes to either flow through correctly.
 *
 * Stage 2 will port the upload_* functions from
 * libs/raytrace/opengl/renderer.c and start filling those SSBOs with
 * real scene data. That's where colour shows up.
 *
 * Descriptor layout (single pipeline, three sets to keep image / SSBO /
 * sampler bindings from colliding inside Vulkan's unified namespace):
 *   set 0 binding 0  storage image  outputImage      (rgba8)
 *   set 0 binding 1  storage image  gObjectId        (r32ui)
 *   set 0 binding 2  storage image  gDepth           (r32f)
 *   set 0 binding 3  storage image  gNormal          (rgba32f)
 *   set 0 binding 4  uniform buffer Globals          (std140)
 *   set 1 binding 0..15  storage buffers (scene data — dummy in Stage 1)
 *   set 2 binding 0..1   combined image samplers (sprite/tex atlas — dummy 1x1) */

#include "renderer.h"
#include "scene.h"
#include "viewport.h"

#include <vulkan/vulkan.h>

#include "raytrace_spv.h"  /* generated: raytrace_spv[], raytrace_spv_len */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_CHECK(expr) do {                                                   \
    VkResult _r = (expr);                                                     \
    if (_r != VK_SUCCESS) {                                                   \
        fprintf(stderr, "vulkan: %s -> %d\n", #expr, (int)_r);                \
        return _r;                                                            \
    }                                                                         \
} while (0)

#define SSBO_COUNT 16

/* Mirrors the std140 Globals block in raytrace.comp. vec3 members occupy
 * 16 bytes each (vec3 alignment is 16 in std140). */
typedef struct {
    int32_t have_gbuf;
    int32_t _pad0[3];
    float   cam_origin[3];   float _pad1;
    float   cam_forward[3];  float _pad2;
    float   cam_right[3];    float _pad3;
    float   cam_up[3];       float _pad4;
    float   fov;
    float   ambient;
    int32_t sphere_count;
    int32_t plane_count;
    int32_t disc_count;
    int32_t cylinder_count;
    int32_t cone_count;
    int32_t torus_count;
    int32_t triangle_count;
    int32_t box_count;
    int32_t sprite_count;
    int32_t heightfield_count;
    int32_t light_count;
    int32_t mesh_count;
    int32_t material_count;
} vk_globals;

typedef struct {
    VkImage         image;
    VkDeviceMemory  mem;
    VkImageView     view;
} vk_image;

typedef struct {
    VkInstance       instance;
    VkPhysicalDevice phys;
    VkDevice         device;
    uint32_t         queue_family;
    VkQueue          queue;
    VkCommandPool    pool;
    VkCommandBuffer  cmd;
    VkFence          fence;

    /* Pipeline + descriptor objects (created once at startup). */
    VkShaderModule        shader;
    VkDescriptorSetLayout set_layouts[3];
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipeline;
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       desc_sets[3];

    /* Constant-size resources. */
    VkBuffer       ubo;
    VkDeviceMemory ubo_mem;
    void          *ubo_mapped;
    VkBuffer       dummy_ssbo;
    VkDeviceMemory dummy_ssbo_mem;
    vk_image       dummy_array;     /* 1x1x1 sampler2DArray placeholder */
    VkSampler      dummy_sampler;

    /* Per-size resources, recreated on resize. */
    int            w, h;
    vk_image       output;
    vk_image       g_id, g_depth, g_normal;
    VkBuffer       readback;
    VkDeviceMemory readback_mem;
    void          *readback_mapped;
} vk_backend_data;

/* -- Small helpers ---------------------------------------------------- */

static int find_memory_type(VkPhysicalDevice phys,
                            uint32_t type_bits,
                            VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want) {
            return (int)i;
        }
    }
    return -1;
}

static void barrier_image(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout from, VkImageLayout to,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier b = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = src_access,
        .dstAccessMask       = dst_access,
        .oldLayout           = from,
        .newLayout           = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

static int create_image(vk_backend_data *d, vk_image *out,
                        VkFormat fmt, uint32_t w, uint32_t h,
                        uint32_t array_layers,
                        VkImageUsageFlags usage,
                        VkImageViewType view_type) {
    VkImageCreateInfo ici = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = fmt,
        .extent        = { w, h, 1 },
        .mipLevels     = 1,
        .arrayLayers   = array_layers,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(d->device, &ici, NULL, &out->image) != VK_SUCCESS) return -1;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d->device, out->image, &mr);
    int mt = find_memory_type(d->phys, mr.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt < 0) return -1;
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mr.size,
        .memoryTypeIndex = (uint32_t)mt,
    };
    if (vkAllocateMemory(d->device, &mai, NULL, &out->mem) != VK_SUCCESS) return -1;
    vkBindImageMemory(d->device, out->image, out->mem, 0);

    VkImageViewCreateInfo vci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = out->image,
        .viewType         = view_type,
        .format           = fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, array_layers },
    };
    if (vkCreateImageView(d->device, &vci, NULL, &out->view) != VK_SUCCESS) return -1;
    return 0;
}

static void destroy_image(VkDevice dev, vk_image *im) {
    if (im->view)  { vkDestroyImageView(dev, im->view, NULL);  im->view = VK_NULL_HANDLE; }
    if (im->image) { vkDestroyImage(dev, im->image, NULL);     im->image = VK_NULL_HANDLE; }
    if (im->mem)   { vkFreeMemory(dev, im->mem, NULL);         im->mem = VK_NULL_HANDLE; }
}

static int create_buffer(vk_backend_data *d, VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags mem_props,
                         VkBuffer *out_buf, VkDeviceMemory *out_mem,
                         void **out_mapped) {
    VkBufferCreateInfo bci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(d->device, &bci, NULL, out_buf) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(d->device, *out_buf, &mr);
    int mt = find_memory_type(d->phys, mr.memoryTypeBits, mem_props);
    if (mt < 0) return -1;
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mr.size,
        .memoryTypeIndex = (uint32_t)mt,
    };
    if (vkAllocateMemory(d->device, &mai, NULL, out_mem) != VK_SUCCESS) return -1;
    vkBindBufferMemory(d->device, *out_buf, *out_mem, 0);
    if (out_mapped) {
        if (vkMapMemory(d->device, *out_mem, 0, size, 0, out_mapped) != VK_SUCCESS)
            return -1;
    }
    return 0;
}

/* -- Descriptor writes ------------------------------------------------ */

static void write_set0(vk_backend_data *d) {
    VkDescriptorImageInfo img[4] = {
        { VK_NULL_HANDLE, d->output.view,   VK_IMAGE_LAYOUT_GENERAL },
        { VK_NULL_HANDLE, d->g_id.view,     VK_IMAGE_LAYOUT_GENERAL },
        { VK_NULL_HANDLE, d->g_depth.view,  VK_IMAGE_LAYOUT_GENERAL },
        { VK_NULL_HANDLE, d->g_normal.view, VK_IMAGE_LAYOUT_GENERAL },
    };
    VkDescriptorBufferInfo ubo_info = { d->ubo, 0, sizeof(vk_globals) };
    VkWriteDescriptorSet w[5] = {0};
    for (int i = 0; i < 4; ++i) {
        w[i] = (VkWriteDescriptorSet){
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = d->desc_sets[0],
            .dstBinding      = (uint32_t)i,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = &img[i],
        };
    }
    w[4] = (VkWriteDescriptorSet){
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = d->desc_sets[0],
        .dstBinding      = 4,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &ubo_info,
    };
    vkUpdateDescriptorSets(d->device, 5, w, 0, NULL);
}

static void write_set1_dummy(vk_backend_data *d) {
    /* Bind the same dummy zero buffer at every SSBO slot. The shader's
     * count uniforms are all 0 in Stage 1, so the buffers are never
     * read — but Vulkan validation still needs each binding populated. */
    VkDescriptorBufferInfo info[SSBO_COUNT];
    VkWriteDescriptorSet   w[SSBO_COUNT];
    for (int i = 0; i < SSBO_COUNT; ++i) {
        info[i] = (VkDescriptorBufferInfo){ d->dummy_ssbo, 0, VK_WHOLE_SIZE };
        w[i] = (VkWriteDescriptorSet){
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = d->desc_sets[1],
            .dstBinding      = (uint32_t)i,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &info[i],
        };
    }
    vkUpdateDescriptorSets(d->device, SSBO_COUNT, w, 0, NULL);
}

static void write_set2_dummy(vk_backend_data *d) {
    VkDescriptorImageInfo info[2] = {
        { d->dummy_sampler, d->dummy_array.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { d->dummy_sampler, d->dummy_array.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    VkWriteDescriptorSet w[2] = {0};
    for (int i = 0; i < 2; ++i) {
        w[i] = (VkWriteDescriptorSet){
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = d->desc_sets[2],
            .dstBinding      = (uint32_t)i,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &info[i],
        };
    }
    vkUpdateDescriptorSets(d->device, 2, w, 0, NULL);
}

/* -- Per-size resources ---------------------------------------------- */

static void free_per_size(vk_backend_data *d) {
    if (d->readback_mapped) {
        vkUnmapMemory(d->device, d->readback_mem);
        d->readback_mapped = NULL;
    }
    if (d->readback)     vkDestroyBuffer(d->device, d->readback, NULL);
    if (d->readback_mem) vkFreeMemory(d->device, d->readback_mem, NULL);
    d->readback = VK_NULL_HANDLE; d->readback_mem = VK_NULL_HANDLE;
    destroy_image(d->device, &d->output);
    destroy_image(d->device, &d->g_id);
    destroy_image(d->device, &d->g_depth);
    destroy_image(d->device, &d->g_normal);
}

static int ensure_size(vk_backend_data *d, int w, int h) {
    if (d->w == w && d->h == h && d->output.image) return 0;
    vkDeviceWaitIdle(d->device);
    free_per_size(d);

    if (create_image(d, &d->output, VK_FORMAT_B8G8R8A8_UNORM, (uint32_t)w, (uint32_t)h, 1,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_VIEW_TYPE_2D) != 0) return -1;
    /* G-buffer images: bound to satisfy descriptor layout; never written
     * because u_have_gbuf stays 0 in Stage 1. */
    if (create_image(d, &d->g_id,     VK_FORMAT_R32_UINT,          (uint32_t)w, (uint32_t)h, 1,
                     VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_VIEW_TYPE_2D) != 0) return -1;
    if (create_image(d, &d->g_depth,  VK_FORMAT_R32_SFLOAT,        (uint32_t)w, (uint32_t)h, 1,
                     VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_VIEW_TYPE_2D) != 0) return -1;
    if (create_image(d, &d->g_normal, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)w, (uint32_t)h, 1,
                     VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_VIEW_TYPE_2D) != 0) return -1;

    VkDeviceSize buf_size = (VkDeviceSize)w * (VkDeviceSize)h * 4;
    if (create_buffer(d, buf_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &d->readback, &d->readback_mem, &d->readback_mapped) != 0)
        return -1;

    /* Transition all storage images to GENERAL once; they stay there. */
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkResetCommandBuffer(d->cmd, 0);
    vkBeginCommandBuffer(d->cmd, &bi);
    VkImage imgs[4] = { d->output.image, d->g_id.image, d->g_depth.image, d->g_normal.image };
    for (int i = 0; i < 4; ++i) {
        barrier_image(d->cmd, imgs[i],
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                      0, VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    vkEndCommandBuffer(d->cmd);
    VkSubmitInfo si = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &d->cmd,
    };
    vkResetFences(d->device, 1, &d->fence);
    vkQueueSubmit(d->queue, 1, &si, d->fence);
    vkWaitForFences(d->device, 1, &d->fence, VK_TRUE, UINT64_MAX);

    d->w = w; d->h = h;
    write_set0(d);  /* descriptor set 0 references the new image views */
    return 0;
}

/* -- Per-frame render ------------------------------------------------- */

static void vulkan_render(rt_renderer *r,
                          const scene *scene_in,
                          const scene_camera *camera,
                          const rt_viewport *viewport,
                          uint32_t *pixels,
                          rt_gbuffer *gbuf) {
    (void)gbuf;
    vk_backend_data *d = r->backend_data;
    if (ensure_size(d, viewport->width, viewport->height) != 0) return;

    /* Update UBO: camera + ambient + viewport, all counts forced to 0. */
    vector origin, forward, right, up;
    scene_camera_get_basis(camera, &origin, &forward, &right, &up);
    vk_globals g = {0};
    g.have_gbuf       = 0;
    g.cam_origin[0]   = origin.x;  g.cam_origin[1]   = origin.y;  g.cam_origin[2]   = origin.z;
    g.cam_forward[0]  = forward.x; g.cam_forward[1]  = forward.y; g.cam_forward[2]  = forward.z;
    g.cam_right[0]    = right.x;   g.cam_right[1]    = right.y;   g.cam_right[2]    = right.z;
    g.cam_up[0]       = up.x;      g.cam_up[1]       = up.y;      g.cam_up[2]       = up.z;
    g.fov             = viewport->fov;
    g.ambient         = scene_in->ambient;
    /* Stage 2 will populate counts + SSBOs. Until then everything misses
     * and the frame is the bare miss-path output. */
    memcpy(d->ubo_mapped, &g, sizeof(g));

    vkResetCommandBuffer(d->cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(d->cmd, &bi);

    vkCmdBindPipeline(d->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline);
    vkCmdBindDescriptorSets(d->cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            d->pipeline_layout, 0, 3, d->desc_sets, 0, NULL);
    uint32_t gx = ((uint32_t)d->w + 15) / 16;
    uint32_t gy = ((uint32_t)d->h + 15) / 16;
    vkCmdDispatch(d->cmd, gx, gy, 1);

    /* Output image: GENERAL (shader writes) → TRANSFER_SRC for the copy. */
    barrier_image(d->cmd, d->output.image,
                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset       = { 0, 0, 0 },
        .imageExtent       = { (uint32_t)d->w, (uint32_t)d->h, 1 },
    };
    vkCmdCopyImageToBuffer(d->cmd, d->output.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           d->readback, 1, &copy);

    /* Back to GENERAL for next frame's shader writes. */
    barrier_image(d->cmd, d->output.image,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkEndCommandBuffer(d->cmd);

    VkSubmitInfo si = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &d->cmd,
    };
    vkResetFences(d->device, 1, &d->fence);
    vkQueueSubmit(d->queue, 1, &si, d->fence);
    vkWaitForFences(d->device, 1, &d->fence, VK_TRUE, UINT64_MAX);

    memcpy(pixels, d->readback_mapped, (size_t)d->w * (size_t)d->h * 4);
}

static const char *vulkan_name(const rt_renderer *r) { (void)r; return "Vulkan"; }

static void vulkan_destroy(rt_renderer *r) {
    vk_backend_data *d = r->backend_data;
    if (d->device) {
        vkDeviceWaitIdle(d->device);
        free_per_size(d);
        if (d->dummy_sampler)  vkDestroySampler(d->device, d->dummy_sampler, NULL);
        destroy_image(d->device, &d->dummy_array);
        if (d->dummy_ssbo)     vkDestroyBuffer(d->device, d->dummy_ssbo, NULL);
        if (d->dummy_ssbo_mem) vkFreeMemory(d->device, d->dummy_ssbo_mem, NULL);
        if (d->ubo_mapped)     vkUnmapMemory(d->device, d->ubo_mem);
        if (d->ubo)            vkDestroyBuffer(d->device, d->ubo, NULL);
        if (d->ubo_mem)        vkFreeMemory(d->device, d->ubo_mem, NULL);
        if (d->desc_pool)      vkDestroyDescriptorPool(d->device, d->desc_pool, NULL);
        for (int i = 0; i < 3; ++i)
            if (d->set_layouts[i]) vkDestroyDescriptorSetLayout(d->device, d->set_layouts[i], NULL);
        if (d->pipeline)        vkDestroyPipeline(d->device, d->pipeline, NULL);
        if (d->pipeline_layout) vkDestroyPipelineLayout(d->device, d->pipeline_layout, NULL);
        if (d->shader)          vkDestroyShaderModule(d->device, d->shader, NULL);
        if (d->fence)           vkDestroyFence(d->device, d->fence, NULL);
        if (d->pool)            vkDestroyCommandPool(d->device, d->pool, NULL);
        vkDestroyDevice(d->device, NULL);
    }
    if (d->instance) vkDestroyInstance(d->instance, NULL);
    free(d);
    free(r);
}

/* -- Construction ----------------------------------------------------- */

static int pick_physical_device(vk_backend_data *d) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(d->instance, &n, NULL);
    if (n == 0) return -1;
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(d->instance, &n, devs);
    d->phys = VK_NULL_HANDLE;
    d->queue_family = UINT32_MAX;
    for (uint32_t pass = 0; pass < 2 && d->phys == VK_NULL_HANDLE; ++pass) {
        for (uint32_t i = 0; i < n; ++i) {
            VkPhysicalDeviceProperties pp;
            vkGetPhysicalDeviceProperties(devs[i], &pp);
            if (pass == 0 && pp.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
            VkQueueFamilyProperties *qfs = calloc(qn, sizeof(*qfs));
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, qfs);
            for (uint32_t q = 0; q < qn; ++q) {
                if (qfs[q].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    d->phys = devs[i];
                    d->queue_family = q;
                    break;
                }
            }
            free(qfs);
            if (d->phys != VK_NULL_HANDLE) break;
        }
    }
    free(devs);
    return d->phys == VK_NULL_HANDLE ? -1 : 0;
}

static int create_descriptor_layouts(vk_backend_data *d) {
    /* set 0: 4 storage images + 1 UBO */
    VkDescriptorSetLayoutBinding b0[5] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
    };
    VkDescriptorSetLayoutCreateInfo ci0 = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 5, .pBindings = b0,
    };
    if (vkCreateDescriptorSetLayout(d->device, &ci0, NULL, &d->set_layouts[0]) != VK_SUCCESS)
        return -1;

    /* set 1: 16 storage buffers */
    VkDescriptorSetLayoutBinding b1[SSBO_COUNT];
    for (int i = 0; i < SSBO_COUNT; ++i) {
        b1[i] = (VkDescriptorSetLayoutBinding){
            (uint32_t)i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, NULL,
        };
    }
    VkDescriptorSetLayoutCreateInfo ci1 = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = SSBO_COUNT, .pBindings = b1,
    };
    if (vkCreateDescriptorSetLayout(d->device, &ci1, NULL, &d->set_layouts[1]) != VK_SUCCESS)
        return -1;

    /* set 2: 2 combined image samplers */
    VkDescriptorSetLayoutBinding b2[2] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
    };
    VkDescriptorSetLayoutCreateInfo ci2 = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = b2,
    };
    if (vkCreateDescriptorSetLayout(d->device, &ci2, NULL, &d->set_layouts[2]) != VK_SUCCESS)
        return -1;
    return 0;
}

static int create_pipeline(vk_backend_data *d) {
    VkShaderModuleCreateInfo sci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = raytrace_spv_len,
        .pCode    = (const uint32_t *)raytrace_spv,
    };
    if (vkCreateShaderModule(d->device, &sci, NULL, &d->shader) != VK_SUCCESS) return -1;

    VkPipelineLayoutCreateInfo plci = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 3,
        .pSetLayouts    = d->set_layouts,
    };
    if (vkCreatePipelineLayout(d->device, &plci, NULL, &d->pipeline_layout) != VK_SUCCESS)
        return -1;

    VkComputePipelineCreateInfo pci = {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = d->shader,
            .pName  = "main",
        },
        .layout = d->pipeline_layout,
    };
    if (vkCreateComputePipelines(d->device, VK_NULL_HANDLE, 1, &pci, NULL, &d->pipeline) != VK_SUCCESS)
        return -1;
    return 0;
}

static int create_dummy_array(vk_backend_data *d) {
    if (create_image(d, &d->dummy_array, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 1,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_VIEW_TYPE_2D_ARRAY) != 0) return -1;

    /* Transition UNDEFINED → SHADER_READ_ONLY_OPTIMAL (no actual content
     * needed — the shader never samples it since counts are zero). */
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkResetCommandBuffer(d->cmd, 0);
    vkBeginCommandBuffer(d->cmd, &bi);
    barrier_image(d->cmd, d->dummy_array.image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkEndCommandBuffer(d->cmd);
    VkSubmitInfo si = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &d->cmd,
    };
    vkResetFences(d->device, 1, &d->fence);
    vkQueueSubmit(d->queue, 1, &si, d->fence);
    vkWaitForFences(d->device, 1, &d->fence, VK_TRUE, UINT64_MAX);

    VkSamplerCreateInfo sai = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_NEAREST,
        .minFilter    = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(d->device, &sai, NULL, &d->dummy_sampler) != VK_SUCCESS) return -1;
    return 0;
}

rt_renderer *rt_vulkan_renderer_create(void) {
    rt_renderer *r = calloc(1, sizeof(*r));
    vk_backend_data *d = calloc(1, sizeof(*d));
    if (!r || !d) { free(r); free(d); return NULL; }

    VkApplicationInfo app = {
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "c-monorepo",
        .apiVersion       = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo ici = {
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    if (vkCreateInstance(&ici, NULL, &d->instance) != VK_SUCCESS) goto fail;
    if (pick_physical_device(d) != 0) goto fail;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = d->queue_family,
        .queueCount       = 1,
        .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo dci = {
        .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos    = &qci,
    };
    if (vkCreateDevice(d->phys, &dci, NULL, &d->device) != VK_SUCCESS) goto fail;
    vkGetDeviceQueue(d->device, d->queue_family, 0, &d->queue);

    VkCommandPoolCreateInfo pci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = d->queue_family,
    };
    vkCreateCommandPool(d->device, &pci, NULL, &d->pool);
    VkCommandBufferAllocateInfo cbai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = d->pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(d->device, &cbai, &d->cmd);
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(d->device, &fci, NULL, &d->fence);

    if (create_descriptor_layouts(d) != 0) goto fail;
    if (create_pipeline(d) != 0)           goto fail;

    /* UBO (host-visible, persistently mapped). */
    if (create_buffer(d, sizeof(vk_globals),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &d->ubo, &d->ubo_mem, &d->ubo_mapped) != 0) goto fail;
    memset(d->ubo_mapped, 0, sizeof(vk_globals));

    /* Dummy SSBO — bound to every set-1 slot in Stage 1. */
    if (create_buffer(d, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &d->dummy_ssbo, &d->dummy_ssbo_mem, NULL) != 0) goto fail;

    if (create_dummy_array(d) != 0) goto fail;

    /* Descriptor pool + sets. */
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,         4 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,        1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        SSBO_COUNT },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 3,
        .poolSizeCount = (uint32_t)(sizeof(sizes)/sizeof(sizes[0])),
        .pPoolSizes    = sizes,
    };
    if (vkCreateDescriptorPool(d->device, &dpci, NULL, &d->desc_pool) != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo dsai = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = d->desc_pool,
        .descriptorSetCount = 3,
        .pSetLayouts        = d->set_layouts,
    };
    if (vkAllocateDescriptorSets(d->device, &dsai, d->desc_sets) != VK_SUCCESS) goto fail;

    /* Writes that don't depend on size happen now; set 0 (which includes
     * the storage images) is written from ensure_size. */
    write_set1_dummy(d);
    write_set2_dummy(d);

    r->destroy_fn       = vulkan_destroy;
    r->render_fn        = vulkan_render;
    r->name_fn          = vulkan_name;
    r->set_interlace_fn = NULL;
    r->backend_data     = d;
    return r;

fail:
    fprintf(stderr, "vulkan: backend init failed\n");
    if (d->device) { vkDeviceWaitIdle(d->device); vkDestroyDevice(d->device, NULL); }
    if (d->instance) vkDestroyInstance(d->instance, NULL);
    free(d); free(r);
    return NULL;
}

#endif /* RT_HAVE_VULKAN_BACKEND */
