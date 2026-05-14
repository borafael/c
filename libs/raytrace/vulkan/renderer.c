#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef RT_HAVE_VULKAN_BACKEND

/* Vulkan backend stub.
 *
 * Proves the plumbing end-to-end: instance, physical device pick, logical
 * device + compute queue, command pool/buffer, fence, host-readable
 * image, transfer-to-buffer readback, memcpy into the caller's pixels.
 *
 * No compute pipeline yet — every frame is a vkCmdClearColorImage with
 * a frame-counter-driven hue so the host sees the screen change colour
 * and knows the GPU dispatch and CPU readback are both alive. The next
 * step is to add a descriptor set + compute pipeline + SPIR-V module,
 * port the GLSL from libs/raytrace/opengl/renderer.c, and replace the
 * clear with a vkCmdDispatch over a width/height workgroup grid.
 *
 * Scene/camera/viewport/gbuf are deliberately unused for now. */

#include "renderer.h"

#include <vulkan/vulkan.h>

#include <math.h>
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

typedef struct {
    VkInstance       instance;
    VkPhysicalDevice phys;
    VkDevice         device;
    uint32_t         queue_family;
    VkQueue          queue;
    VkCommandPool    pool;
    VkCommandBuffer  cmd;
    VkFence          fence;

    /* Per-size resources, recreated when width/height changes. */
    int              w, h;
    VkImage          image;
    VkDeviceMemory   image_mem;
    VkBuffer         readback;
    VkDeviceMemory   readback_mem;
    void            *readback_mapped;

    uint32_t         frame;
} vk_backend_data;

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

static void transition_layout(VkCommandBuffer cmd, VkImage image,
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

static void free_size_resources(vk_backend_data *d) {
    if (d->readback_mapped) {
        vkUnmapMemory(d->device, d->readback_mem);
        d->readback_mapped = NULL;
    }
    if (d->readback)     { vkDestroyBuffer(d->device, d->readback, NULL);  d->readback = VK_NULL_HANDLE; }
    if (d->readback_mem) { vkFreeMemory(d->device, d->readback_mem, NULL); d->readback_mem = VK_NULL_HANDLE; }
    if (d->image)        { vkDestroyImage(d->device, d->image, NULL);      d->image = VK_NULL_HANDLE; }
    if (d->image_mem)    { vkFreeMemory(d->device, d->image_mem, NULL);    d->image_mem = VK_NULL_HANDLE; }
}

static int ensure_size(vk_backend_data *d, int w, int h) {
    if (d->w == w && d->h == h && d->image) return 0;
    vkDeviceWaitIdle(d->device);
    free_size_resources(d);

    VkImageCreateInfo ici = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_B8G8R8A8_UNORM,
        .extent        = { (uint32_t)w, (uint32_t)h, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                       | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(d->device, &ici, NULL, &d->image));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d->device, d->image, &mr);
    int mt = find_memory_type(d->phys, mr.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt < 0) { fprintf(stderr, "vulkan: no device-local memory type\n"); return -1; }
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mr.size,
        .memoryTypeIndex = (uint32_t)mt,
    };
    VK_CHECK(vkAllocateMemory(d->device, &mai, NULL, &d->image_mem));
    VK_CHECK(vkBindImageMemory(d->device, d->image, d->image_mem, 0));

    VkDeviceSize buf_size = (VkDeviceSize)w * (VkDeviceSize)h * 4;
    VkBufferCreateInfo bci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = buf_size,
        .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(d->device, &bci, NULL, &d->readback));
    vkGetBufferMemoryRequirements(d->device, d->readback, &mr);
    mt = find_memory_type(d->phys, mr.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt < 0) { fprintf(stderr, "vulkan: no host-visible memory type\n"); return -1; }
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = (uint32_t)mt;
    VK_CHECK(vkAllocateMemory(d->device, &mai, NULL, &d->readback_mem));
    VK_CHECK(vkBindBufferMemory(d->device, d->readback, d->readback_mem, 0));
    VK_CHECK(vkMapMemory(d->device, d->readback_mem, 0, buf_size, 0,
                         &d->readback_mapped));

    d->w = w;
    d->h = h;
    return 0;
}

static void vulkan_render(rt_renderer *r,
                          const scene *scene_in,
                          const scene_camera *camera,
                          const rt_viewport *viewport,
                          uint32_t *pixels,
                          rt_gbuffer *gbuf) {
    (void)scene_in; (void)camera; (void)gbuf;
    vk_backend_data *d = r->backend_data;
    if (ensure_size(d, viewport->width, viewport->height) != 0) return;

    /* Time-varying clear colour so the host sees motion. Hue cycles
     * across frames; replace with a compute dispatch once a pipeline
     * is wired in. */
    float t = (float)(d->frame++) * 0.02f;
    VkClearColorValue clear = {
        .float32 = {
            0.5f + 0.5f * sinf(t),
            0.5f + 0.5f * sinf(t + 2.094f),  /* +120° */
            0.5f + 0.5f * sinf(t + 4.189f),  /* +240° */
            1.0f,
        },
    };

    vkResetCommandBuffer(d->cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(d->cmd, &bi);

    transition_layout(d->cmd, d->image,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(d->cmd, d->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);

    transition_layout(d->cmd, d->image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,  /* tightly packed */
        .bufferImageHeight = 0,
        .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset       = { 0, 0, 0 },
        .imageExtent       = { (uint32_t)d->w, (uint32_t)d->h, 1 },
    };
    vkCmdCopyImageToBuffer(d->cmd, d->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           d->readback, 1, &copy);

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

static const char *vulkan_name(const rt_renderer *r) { (void)r; return "Vulkan (stub)"; }

static void vulkan_destroy(rt_renderer *r) {
    vk_backend_data *d = r->backend_data;
    if (d->device) {
        vkDeviceWaitIdle(d->device);
        free_size_resources(d);
        if (d->fence) vkDestroyFence(d->device, d->fence, NULL);
        if (d->pool)  vkDestroyCommandPool(d->device, d->pool, NULL);
        vkDestroyDevice(d->device, NULL);
    }
    if (d->instance) vkDestroyInstance(d->instance, NULL);
    free(d);
    free(r);
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
    if (vkCreateInstance(&ici, NULL, &d->instance) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateInstance failed\n");
        goto fail;
    }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(d->instance, &n, NULL);
    if (n == 0) { fprintf(stderr, "vulkan: no physical devices\n"); goto fail; }
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(d->instance, &n, devs);

    /* Pick the first device with a compute-capable queue family. Prefer
     * discrete if present. */
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
    if (d->phys == VK_NULL_HANDLE) {
        fprintf(stderr, "vulkan: no compute-capable queue family\n");
        goto fail;
    }

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
    if (vkCreateDevice(d->phys, &dci, NULL, &d->device) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateDevice failed\n");
        goto fail;
    }
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

    r->destroy_fn       = vulkan_destroy;
    r->render_fn        = vulkan_render;
    r->name_fn          = vulkan_name;
    r->set_interlace_fn = NULL;
    r->backend_data     = d;
    return r;

fail:
    if (d->device)   vkDestroyDevice(d->device, NULL);
    if (d->instance) vkDestroyInstance(d->instance, NULL);
    free(d);
    free(r);
    return NULL;
}

#endif /* RT_HAVE_VULKAN_BACKEND */
