#define VOLK_IMPLEMENTATION
#include "volk.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "ctl/allocator.hpp"
#include "ctl/array.hpp"
#include "ctl/file.hpp"
#include "macro_utils.hpp"

using namespace ctl;

constexpr Ulen NUM_FRAMES_IN_FLIGHT = 2;
constexpr Ulen NUM_SWAPCHAIN_IMAGES = 3;

static Array<Uint8> loadFile(Allocator& allocator, StringView fileName) {
    auto fd = File::open(fileName, File::Access::RD);
    if (fd.is_valid()) {
        defer(fd->close());
        return fd->map(allocator);
    } else {
        fprintf(stderr, "Could not load file %.*s\n", (Sint32)fileName.length(), fileName.data());
        exit(1);
    }
}

#define vkCheck(result) check_location((result), __FILE__, __LINE__)
static inline void check_location(VkResult result, const char *filePath, Uint32 line) {
    // TODO: create cases on different error
    if (result != VK_SUCCESS) {
        fprintf(stderr, "%s:%d:Vulkan failure: %d\n", filePath, line, result);
        abort();
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
    VkFormat imageFormat;
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
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
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

static void createGraphicsPipeline(Context& ctx, TemporaryAllocator& temp_alloc);

static void destroyGraphicsPipeline(Context& ctx);

static void render(Context& ctx, VkCommandBuffer cmd) {
    // bind the grahpic pipeline
    vkCmdBindPipeline(cmd,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx.pipeline);

    // dynamic viewport
    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width  = (float)ctx.swapchain.width,
        .height = (float)ctx.swapchain.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // dynamic Scissor
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {
            ctx.swapchain.width,
            ctx.swapchain.height
        }
    };

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    struct Push {
        Float32 color[3];
    };

    Push push{
        { 0.0f, 0.5f, 0.0f }
    };

    vkCmdPushConstants(cmd,
                       ctx.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(Push),
                       &push);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

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

    createGraphicsPipeline(ctx, temp_alloc);
    defer(destroyGraphicsPipeline(ctx));

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
        render(ctx, cmd);

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
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_KHRONOS_shader_object",
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
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        //VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
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
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = candidate;
            break;
        }
    }
    ctx.swapchain.imageFormat = surfaceFormat.format;

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

static void createGraphicsPipeline(Context& ctx, TemporaryAllocator& temp_alloc) {
    Array<Uint8> fragCode = loadFile(temp_alloc, "shaders/spv/shader.frag.spv");
    Array<Uint8> vertCode = loadFile(temp_alloc, "shaders/spv/shader.vert.spv");

    VkShaderModule vertModule;
    VkShaderModule fragModule;

    VkShaderModuleCreateInfo moduleCI{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    moduleCI.codeSize = vertCode.length();
    moduleCI.pCode = (Uint32*)vertCode.data();
    vkCheck(vkCreateShaderModule(ctx.device, &moduleCI, nullptr, &vertModule));
    defer(vkDestroyShaderModule(ctx.device, vertModule, nullptr));

    moduleCI.codeSize = fragCode.length();
    moduleCI.pCode = (Uint32*)fragCode.data();
    vkCheck(vkCreateShaderModule(ctx.device, &moduleCI, nullptr, &fragModule));
    defer(vkDestroyShaderModule(ctx.device, fragModule, nullptr));

    // --- Shader stages ---
    VkPipelineShaderStageCreateInfo stages[2]{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName = "main"
        }
    };

    // --- Pipeline layout (push constants) ---
    VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = 128
    };

    VkPipelineLayoutCreateInfo layoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };

    vkCheck(vkCreatePipelineLayout(ctx.device, &layoutCI, nullptr, &ctx.pipelineLayout));

    // --- Dynamic states ---
    VkDynamicState dynamics[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = STATIC_LEN(dynamics),
        .pDynamicStates = dynamics
    };

    // --- Fixed states ---
    VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    VkPipelineRasterizationStateCreateInfo raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo msaa{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineColorBlendAttachmentState blendAttachment{
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo blend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment
    };

    VkPipelineRenderingCreateInfo renderingCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &ctx.swapchain.imageFormat
    };

    VkGraphicsPipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &raster,
        .pMultisampleState = &msaa,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamicCI,
        .layout = ctx.pipelineLayout
    };

    vkCheck(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &ctx.pipeline));

}

static void destroyGraphicsPipeline(Context& ctx) {
    vkDestroyPipeline(ctx.device, ctx.pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, ctx.pipelineLayout, nullptr);
}
