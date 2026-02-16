#define VOLK_IMPLEMENTATION
#include "volk.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <assert.h>
#include "ctl/allocator.hpp"
#include "ctl/array.hpp"
#include "macro_utils.hpp"

using namespace ctl;

constexpr Ulen NUM_FRAMES_IN_FLIGHT = 2;
constexpr Ulen NUM_SWAPCHAIN_IMAGES = 3;

#define vkCheck(result) check_location((result), __FILE__, __LINE__)
static inline void check_location(Bool result, const char *filePath, Uint32 line) {
    // TODO: create cases on different error
    if (result != VK_SUCCESS) {
        fprintf(stderr, "%s:%d:Vulkan failure: %d\n", filePath, line, result);
    }
}

struct PerFrameData {
    VkFence fence;
    VkSemaphore acquireSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

struct Swapchain {
    constexpr Swapchain(Allocator& allocator)
        : images{allocator}
        , imageViews{allocator}
        , presentSemaphores{allocator}
    {}

    VkSwapchainKHR handle;
    Uint32 width;
    Uint32 height;
    Array<VkImage> images;
    Array<VkImageView> imageViews;
    Array<VkSemaphore> presentSemaphores;
};

struct Context {
    constexpr Context(Allocator& allocator)
        : allocator{allocator}
        , swapchain{allocator}
    {}

    Allocator& allocator;
    GLFWwindow *window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    Uint32 queueFamilyIndex;
    VkDevice device;
    VkQueue queue;
    Swapchain swapchain;
    PerFrameData perFrame[NUM_FRAMES_IN_FLIGHT];
    Uint8 frameIndex = 0;
};

static void errorCallback(Sint32 error, const char* description) {
    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
}

static void createInstance(Context& ctx, TemporaryAllocator& temp_alloc);

static void destroyInstance(Context& ctx);

static void createDevice(Context& ctx, TemporaryAllocator& temp_alloc);

static void destroyDevice(Context& ctx);

static void createSwapchain(Context& ctx, TemporaryAllocator& temp_alloc);

static void destroySwapchain(Context& ctx);

static void createFrames(Context& ctx);

static void destroyFrames(Context& ctx);

Sint32 main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc(sys_alloc);
    Context ctx(sys_alloc);

    if (volkInitialize() != VK_SUCCESS) return 1;

    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    if (!glfwVulkanSupported()) return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    ctx.window = glfwCreateWindow(1024, 768, "Hello Vulkan", nullptr, nullptr);
    if (!ctx.window) return 1;
    defer(glfwDestroyWindow(ctx.window));

    createInstance(ctx, temp_alloc);
    defer(destroyInstance(ctx));

    glfwCreateWindowSurface(ctx.instance, ctx.window, nullptr, &ctx.surface);
    defer(vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr));

    createDevice(ctx, temp_alloc);
    defer(destroyDevice(ctx));

    createSwapchain(ctx, temp_alloc);
    defer(destroySwapchain(ctx));

    createFrames(ctx);
    defer(destroyFrames(ctx));

    while (!glfwWindowShouldClose(ctx.window)) {
        temp_alloc.reset();
        glfwPollEvents();

        auto frame = ctx.perFrame[ctx.frameIndex];
        vkCheck(vkWaitForFences(ctx.device, 1, &frame.fence, true, UINT64_MAX));
        vkCheck(vkResetFences(ctx.device, 1, &frame.fence));

        Uint32 imageIndex{0};
        // TODO: handle SUBOPTIMAL_KHR and ERROR_OUT_OF_DATE_KHR
        vkCheck(vkAcquireNextImageKHR(ctx.device, ctx.swapchain.handle, UINT64_MAX, frame.acquireSemaphore, 0, &imageIndex));

        auto presentSemaphore = ctx.swapchain.presentSemaphores[imageIndex];

        vkCheck(vkResetCommandPool(ctx.device, frame.commandPool, {}));
        auto cmd = frame.commandBuffer;

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(cmd, &beginInfo));

        VkImageMemoryBarrier2 transitionToColorAttachmentBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = ctx.swapchain.images[imageIndex],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        VkDependencyInfo dependencyInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &transitionToColorAttachmentBarrier,
        };
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);

        // record GPU commands
        VkRenderingAttachmentInfo colorAttachment {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = ctx.swapchain.imageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {
                .color = { .float32 = {1, 0, 0, 1}}
            }
        };
        VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = { ctx.swapchain.width, ctx.swapchain.height },
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };

        vkCmdBeginRendering(cmd, &renderingInfo);

        // draw stuff

        vkCmdEndRendering(cmd);

        VkImageMemoryBarrier2 transitionToPresentSrcBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = {},
            .dstAccessMask = {},
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = ctx.swapchain.images[imageIndex],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &transitionToPresentSrcBarrier,
        };
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);

        vkCheck(vkEndCommandBuffer(cmd));

        VkPipelineStageFlags waitStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.acquireSemaphore,
            .pWaitDstStageMask = &waitStageFlags,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &presentSemaphore,
        };
        vkCheck(vkQueueSubmit(ctx.queue, 1, &submitInfo, frame.fence));

        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &ctx.swapchain.handle,
            .pImageIndices = &imageIndex,
        };
        // TODO: handle SUBOPTIMAL_KHR and ERROR_OUT_OF_DATE_KHR
        vkCheck(vkQueuePresentKHR(ctx.queue, &presentInfo));

        ctx.frameIndex = (ctx.frameIndex + 1) % NUM_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(ctx.device);

    return 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugUtilsMessengerCallbackEXT(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    const char *level;
    if      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) level = "ERROR";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) level = "WARNING";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) level = "INFO";
    else level = "DEBUG";
    fprintf(stderr, "[%s] --- Validation layer: %s\n", level, pCallbackData->pMessage);
    return VK_FALSE;
}

static void createInstance(Context& ctx, TemporaryAllocator& temp_alloc) {
    const char* layers[] = {
        "VK_LAYER_KHRONOS_validation"
    };

    Array<const char*> extensions(temp_alloc);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    Uint32 instanceExtensionsCount{0};
    auto glfwExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionsCount);
    for (Ulen i = 0; i < instanceExtensionsCount; i++) {
        extensions.push_back(glfwExtensions[i]);
    }

    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCI {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vkDebugUtilsMessengerCallbackEXT,
    };

    VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "SDL3 Vulkan",
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo instanceCI {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &debugMessengerCI,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = STATIC_LEN(layers),
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = (Uint32)extensions.length(),
        .ppEnabledExtensionNames = extensions.data(),
    };

    vkCheck(vkCreateInstance(&instanceCI, nullptr, &ctx.instance));

    volkLoadInstance(ctx.instance);
    assert(vkDestroyInstance != nullptr);

    vkCheck(vkCreateDebugUtilsMessengerEXT(ctx.instance, &debugMessengerCI, nullptr, &ctx.debugMessenger));
}

static void destroyInstance(Context& ctx) {
    vkDestroyDebugUtilsMessengerEXT(ctx.instance, ctx.debugMessenger, nullptr);
    vkDestroyInstance(ctx.instance, nullptr);
}

static void createDevice(Context& ctx, TemporaryAllocator& temp_alloc) {
    Uint32 physicalDeviceCount{0};
    vkCheck(vkEnumeratePhysicalDevices(ctx.instance, &physicalDeviceCount, nullptr));

    Array<VkPhysicalDevice> physicalDevices(temp_alloc);
    physicalDevices.resize(physicalDeviceCount);

    vkCheck(vkEnumeratePhysicalDevices(ctx.instance, &physicalDeviceCount, physicalDevices.data()));

    for (VkPhysicalDevice& candidate : physicalDevices) {
        Uint32 queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

        Array<VkQueueFamilyProperties> queueFamilies(temp_alloc);
        queueFamilies.resize(queueFamilyCount);

        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

        for (Uint32 i = 0; i < queueFamilies.length(); i++) {
            Bool supportsGraphics = queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
            Uint32 supportsPresents = 0;
            vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, ctx.surface, &supportsPresents));

            if (supportsGraphics == true && supportsPresents != 0) {
                ctx.physicalDevice = candidate;
                ctx.queueFamilyIndex = i;
                goto deviceLoop;
            }
        }
    }
 deviceLoop:
    assert(ctx.physicalDevice != nullptr && "No suitable GPU found");

    Float32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = ctx.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        }
    };

    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceVulkan13Features vulkan13Features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = true,
        .dynamicRendering = true,
    };

    VkDeviceCreateInfo deviceCI {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13Features,
        .queueCreateInfoCount = (Uint32)STATIC_LEN(queueCreateInfos),
        .pQueueCreateInfos = queueCreateInfos,
        .enabledExtensionCount = (Uint32)STATIC_LEN(extensions),
        .ppEnabledExtensionNames = extensions,
    };

    vkCheck(vkCreateDevice(ctx.physicalDevice, &deviceCI, 0, &ctx.device));

    volkLoadDevice(ctx.device);
    assert(vkBeginCommandBuffer != nullptr && "Failed to load Vulkan device API");

    vkGetDeviceQueue(ctx.device, ctx.queueFamilyIndex, 0, &ctx.queue);
}

static void destroyDevice(Context& ctx) {
    vkDestroyDevice(ctx.device, nullptr);
}

static void createSwapchain(Context& ctx, TemporaryAllocator& temp_alloc) {
    VkSurfaceCapabilitiesKHR surfaceCaps{0};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));

    Uint32 imageCount = MAX(NUM_SWAPCHAIN_IMAGES, surfaceCaps.minImageCount);
    if (surfaceCaps.maxImageCount != 0) {
        imageCount = MIN(imageCount, surfaceCaps.maxImageCount);
    }

    Uint32 surfaceFormatCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &surfaceFormatCount, nullptr));

    Array<VkSurfaceFormatKHR> surfaceFormats(temp_alloc);
    surfaceFormats.resize(surfaceFormatCount);

    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &surfaceFormatCount, surfaceFormats.data()));

    VkSurfaceFormatKHR surfaceFormat = surfaceFormats[0];
    for (VkSurfaceFormatKHR candidate : surfaceFormats) {
        if (candidate.format & VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace & VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = candidate;
            break;
        }
    }

    Uint32 presentModeCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, nullptr));

    Array<VkPresentModeKHR> presentModes(temp_alloc);
    presentModes.resize(presentModeCount);

    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, presentModes.data()));

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR candidate : presentModes) {
        if (candidate == VK_PRESENT_MODE_FIFO_KHR) {
            presentMode = candidate;
            break;
        }
    }

    Sint32 w = 0, h = 0;
    glfwGetFramebufferSize(ctx.window, &w, &h);
    ctx.swapchain.width = (Uint32)w;
    ctx.swapchain.height = (Uint32)h;

    VkSwapchainCreateInfoKHR swapchainCI = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = {
            .width  = ctx.swapchain.width,
            .height = ctx.swapchain.height,
        },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = true
    };
    vkCheck(vkCreateSwapchainKHR(ctx.device, &swapchainCI, nullptr, &ctx.swapchain.handle));

    vkCheck(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain.handle, &imageCount, nullptr));
    ctx.swapchain.images.resize(imageCount);
    vkCheck(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain.handle, &imageCount, ctx.swapchain.images.data()));

    ctx.swapchain.imageViews.resize(imageCount);
    for (Ulen i = 0; i < ctx.swapchain.images.length(); i++) {
        VkImageViewCreateInfo imageCI {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx.swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        vkCheck(vkCreateImageView(ctx.device, &imageCI, nullptr, &ctx.swapchain.imageViews[i]));
    }

    ctx.swapchain.presentSemaphores.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreCI { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& semaphore : ctx.swapchain.presentSemaphores) {
        vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &semaphore));
    }
}

static void destroySwapchain(Context& ctx) {
    ctx.swapchain.images.destroy();
    for (auto& semaphore : ctx.swapchain.presentSemaphores) {
        vkDestroySemaphore(ctx.device, semaphore, nullptr);
    }
    ctx.swapchain.presentSemaphores.destroy();
    for (auto& imageView : ctx.swapchain.imageViews) {
        vkDestroyImageView(ctx.device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(ctx.device, ctx.swapchain.handle, nullptr);
}

static void createFrames(Context& ctx) {
    for (Ulen i = 0; i < STATIC_LEN(ctx.perFrame); i++) {
        auto& frame = ctx.perFrame[i];
        VkCommandPoolCreateInfo commandPoolCI {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = ctx.queueFamilyIndex,
        };
        vkCheck(vkCreateCommandPool(ctx.device, &commandPoolCI, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo commandBufferAI {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkCheck(vkAllocateCommandBuffers(ctx.device, &commandBufferAI, &frame.commandBuffer));

        VkSemaphoreCreateInfo semaphoreCI { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &frame.acquireSemaphore));

        VkFenceCreateInfo fenceCI {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        vkCheck(vkCreateFence(ctx.device, &fenceCI, nullptr, &frame.fence));
    }
}

static void destroyFrames(Context& ctx) {
    for (Ulen i = 0; i < STATIC_LEN(ctx.perFrame); i++) {
        auto& frame = ctx.perFrame[i];
        vkDestroyCommandPool(ctx.device, frame.commandPool, nullptr);
        vkDestroySemaphore(ctx.device, frame.acquireSemaphore, nullptr);
        vkDestroyFence(ctx.device, frame.fence, nullptr);
    }
}
