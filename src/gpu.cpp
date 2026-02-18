#define VOLK_IMPLEMENTATION
#include "volk.h"
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "gpu.hpp"
#include "macro_utils.hpp"
#include "ctl/array.hpp"
#include "ctl/file.hpp"

using namespace ctl;

constexpr Ulen NUM_FRAMES_IN_FLIGHT = 3;
constexpr Ulen NUM_SWAPCHAIN_IMAGES = 3;
// linear buffer size per frame (16Mb)
constexpr Ulen LINEAR_ALLOCATION_SIZE = 16 * 1024 * 1024;

static const char* VkResultToString(VkResult result) {
    switch (result) {
    #define VK_RESULT(x) case x: return #x;
    #include "vulkan_error.inl"
    default: return "UKNOWN_VK_RESULT";
    }
}

#define vkCheck(result) check_location((result), __FILE__, __LINE__)
static inline void check_location(VkResult result, const char *filePath, Uint32 line) {
    if (result != VK_SUCCESS) {
        fprintf(stderr, "%s:%d:Vulkan failure: %s (%d)\n", filePath, line, VkResultToString(result), result);
        exit(1);
    }
}

static Array<Uint8> loadFile(Allocator& allocator, StringView filePath) {
    auto fd = File::open(filePath, File::Access::RD);
    if (fd.is_valid()) {
        defer(fd->close());
        return fd->map(allocator);
    } else {
        fprintf(stderr, "Could not load file %.*s\n", (Sint32)filePath.length(), filePath.data());
        exit(1);
    }
}

struct LinearAllocator {
    VkBuffer buffer;
    VkDeviceMemory memory;
    Uint64 deviceAddress;
    void* mappedData;
    Uint64 capacity;
    Uint64 currentOffset;

    void init(VkDevice device, VkPhysicalDevice physicalDevice, Uint64 size) {
        capacity = size;
        currentOffset = 0;

        VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        };
        vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, buffer, &memReqs);

        VkMemoryAllocateFlagsInfo allocFlags {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
            .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        };

        VkMemoryAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &allocFlags,
            .allocationSize = memReqs.size,
        };

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        allocInfo.memoryTypeIndex = -1;
        for (Uint32 i = 0; i < memProps.memoryTypeCount; i++) {
            if ((memReqs.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                allocInfo.memoryTypeIndex = i;
                break;
            }
        }
        assert(allocInfo.memoryTypeIndex != -1 && "Failed to find suitable memory type for linear allocator");

        vkCheck(vkAllocateMemory(device, &allocInfo, nullptr, &memory));
        vkCheck(vkBindBufferMemory(device, buffer, memory, 0));
        vkCheck(vkMapMemory(device, memory, 0, size, 0, &mappedData));

        VkBufferDeviceAddressInfo addrInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer,
        };
        deviceAddress = vkGetBufferDeviceAddress(device, &addrInfo);
    }

    void reset() {
        currentOffset = 0;
    }

    gpu::Ptr allocate(Uint64 size, Uint64 alignment) {
        Uint64 padding = (alignment - (currentOffset % alignment)) % alignment;
        currentOffset += padding;

        if (currentOffset + size > capacity) {
            fprintf(stderr, "Linear Allocator Overflow !\n");
            exit(1);
        }

        gpu::Ptr ptr;
        ptr.cpu = (Uint8*)mappedData + currentOffset;
        ptr.gpu = deviceAddress + currentOffset;

        currentOffset += size;
        return ptr;
    }

    void destroy(VkDevice device) {
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
    }
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

struct PerFrameData {
    VkFence fence;
    VkSemaphore acquireSemaphore;
    VkSemaphore renderSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    LinearAllocator allocator;
};

struct Context {
    constexpr Context(Allocator& allocator)
        : swapchain{allocator}
    {}

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    Uint32 queueFamilyIndex = 0;
    VkDevice device;
    VkQueue queue;
    Swapchain swapchain;
    PerFrameData perFrame[NUM_FRAMES_IN_FLIGHT];
    Uint8 frameIndex = 0;
    Uint32 imageIndex = 0;
    VkPipelineLayout pipelineLayout;
};

SystemAllocator sys;
Context ctx{sys};


static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugUtilsMessengerCallbackEXT(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    const char *level;
    if      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) level = "ERROR";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) level = "WARNING";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) level = "INFO";
    else level = "DEBUG";
    fprintf(stderr, "[Validation %s]: %s\n", level, pCallbackData->pMessage);
    return VK_FALSE;
}

static void createInstance(Allocator& alloc) {
    if (volkInitialize() != VK_SUCCESS) exit(1);

    const char* layers[] = {
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_KHRONOS_shader_object",
    };

    Array<const char*> extensions(alloc);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    Uint32 instanceExtensionsCount{0};
    auto instanceExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionsCount);
    for (Ulen i = 0; i < instanceExtensionsCount; i++) {
        extensions.push_back(instanceExtensions[i]);
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

static void createDevice(Allocator& alloc) {
    Uint32 physicalDeviceCount{0};
    vkCheck(vkEnumeratePhysicalDevices(ctx.instance, &physicalDeviceCount, nullptr));

    Array<VkPhysicalDevice> physicalDevices(alloc);
    physicalDevices.resize(physicalDeviceCount);

    vkCheck(vkEnumeratePhysicalDevices(ctx.instance, &physicalDeviceCount, physicalDevices.data()));

    for (VkPhysicalDevice& candidate : physicalDevices) {
        Uint32 queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

        Array<VkQueueFamilyProperties> queueFamilies(alloc);
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

    VkPhysicalDeviceFeatures features10 {
        .shaderInt64 = true,
    };

    VkPhysicalDeviceVulkan12Features vulkan12Features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .runtimeDescriptorArray = true,
        .scalarBlockLayout = true,
        .bufferDeviceAddress = true,
    };

    VkPhysicalDeviceVulkan13Features vulkan13Features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12Features,
        .synchronization2 = true,
        .dynamicRendering = true,
    };

    VkPhysicalDeviceFeatures2 physicalFeatures2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13Features,
        .features = features10,
    };

    VkDeviceCreateInfo deviceCI {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        //.pNext = &vulkan13Features,
        .pNext = &physicalFeatures2,
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

static void createSwapchain(Allocator& alloc, Uint32 width, Uint32 height) {
    VkSurfaceCapabilitiesKHR surfaceCaps{0};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));

    Uint32 imageCount = MAX(NUM_SWAPCHAIN_IMAGES, surfaceCaps.minImageCount);
    if (surfaceCaps.maxImageCount != 0) {
        imageCount = MIN(imageCount, surfaceCaps.maxImageCount);
    }

    Uint32 surfaceFormatCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &surfaceFormatCount, nullptr));

    Array<VkSurfaceFormatKHR> surfaceFormats(alloc);
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

    Array<VkPresentModeKHR> presentModes(alloc);
    presentModes.resize(presentModeCount);

    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, presentModes.data()));

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR candidate : presentModes) {
        if (candidate == VK_PRESENT_MODE_FIFO_KHR) {
            presentMode = candidate;
            break;
        }
    }

    ctx.swapchain.width = width;
    ctx.swapchain.height = height;

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

static void createFrames() {
    VkCommandPoolCreateInfo commandPoolCI {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx.queueFamilyIndex,
    };

    for (Ulen i = 0; i < STATIC_LEN(ctx.perFrame); i++) {
        auto& frame = ctx.perFrame[i];
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
        vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &frame.renderSemaphore));

        VkFenceCreateInfo fenceCI {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        vkCheck(vkCreateFence(ctx.device, &fenceCI, nullptr, &frame.fence));

        frame.allocator.init(ctx.device, ctx.physicalDevice, LINEAR_ALLOCATION_SIZE);
    }
}

namespace gpu {
    void init(TemporaryAllocator& allocator, GLFWwindow *window, Uint32 width, Uint32 height) {
        createInstance(allocator);
        glfwCreateWindowSurface(ctx.instance, window, nullptr, &ctx.surface);
        createDevice(allocator);
        createSwapchain(allocator, width, height);
        createFrames();
    }

    VkCommandBuffer beginFrame() {
        auto& frame = ctx.perFrame[ctx.frameIndex];

        vkCheck(vkWaitForFences(ctx.device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        vkCheck(vkResetFences(ctx.device, 1, &frame.fence));

        frame.allocator.reset();

        VkResult res = vkAcquireNextImageKHR(ctx.device, ctx.swapchain.handle, UINT64_MAX, frame.acquireSemaphore, VK_NULL_HANDLE, &ctx.imageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) return VK_NULL_HANDLE;

        vkResetCommandBuffer(frame.commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

        VkImageMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = ctx.swapchain.images[ctx.imageIndex],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(frame.commandBuffer, &depInfo);

        VkRenderingAttachmentInfo colorAtt {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = ctx.swapchain.imageViews[ctx.imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {
                .color = { .float32 = {0.1f, 0.1f, 0.1f, 0.1f}}
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
            .pColorAttachments = &colorAtt,
        };

        vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

        return frame.commandBuffer;
    }

    void endFrame() {
        auto& frame = ctx.perFrame[ctx.frameIndex];
        VkCommandBuffer cmd = frame.commandBuffer;

        vkCmdEndRendering(cmd);

        VkImageMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = ctx.swapchain.images[ctx.imageIndex],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(cmd, &depInfo);

        vkCheck(vkEndCommandBuffer(cmd));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.acquireSemaphore,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &frame.renderSemaphore,
        };

        vkCheck(vkQueueSubmit(ctx.queue, 1, &submitInfo, frame.fence));

        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.renderSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &ctx.swapchain.handle,
            .pImageIndices = &ctx.imageIndex,
        };

        vkQueuePresentKHR(ctx.queue, &presentInfo);

        ctx.frameIndex = (ctx.frameIndex + 1) % NUM_FRAMES_IN_FLIGHT;
    }

    Ptr malloc(Uint64 size, Uint64 alignment) {
        return ctx.perFrame[ctx.frameIndex].allocator.allocate(size, alignment);
    }

    Pipeline createGraphicsPipeline(TemporaryAllocator& temp_alloc, StringView vertPath, StringView fragPath) {
        Pipeline p{0};
        Array<Uint8> vertCode = loadFile(temp_alloc, vertPath);
        Array<Uint8> fragCode = loadFile(temp_alloc, fragPath);

        VkShaderModule vertModule;
        VkShaderModule fragModule;

        VkShaderModuleCreateInfo moduleCI {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };

        moduleCI.codeSize = vertCode.length();
        moduleCI.pCode = (Uint32*)vertCode.data();
        vkCheck(vkCreateShaderModule(ctx.device, &moduleCI, nullptr, &vertModule));
        defer(vkDestroyShaderModule(ctx.device, vertModule, nullptr));

        moduleCI.codeSize = fragCode.length();
        moduleCI.pCode = (Uint32*)fragCode.data();
        vkCheck(vkCreateShaderModule(ctx.device, &moduleCI, nullptr, &fragModule));
        defer(vkDestroyShaderModule(ctx.device, fragModule, nullptr));

        VkPushConstantRange pushRange {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .offset = 0,
            .size = 128
        };

        VkPipelineLayoutCreateInfo layoutCI {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushRange
        };
        vkCheck(vkCreatePipelineLayout(ctx.device, &layoutCI, nullptr, &p.layout));

        VkPipelineShaderStageCreateInfo stages[2] {
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

        VkPipelineVertexInputStateCreateInfo vertexInput {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        };

        VkPipelineViewportStateCreateInfo viewportState {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        VkPipelineRasterizationStateCreateInfo raster {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo msaa {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        };

        VkPipelineColorBlendAttachmentState blendAttachment {
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

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicCI {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = STATIC_LEN(dynStates),
            .pDynamicStates = dynStates
        };

        VkPipelineRenderingCreateInfo renderingCI {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &ctx.swapchain.imageFormat
        };

        VkGraphicsPipelineCreateInfo pipelineCI {
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
            .layout = p.layout
        };

        vkCheck(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &p.handle));

        return p;
    }

    void destroyPipeline(Pipeline p) {
        vkDestroyPipeline(ctx.device, p.handle, nullptr);
        vkDestroyPipelineLayout(ctx.device, p.layout, nullptr);
    }

    void setPipeline(VkCommandBuffer cmd, Pipeline p) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.handle);

        VkViewport v = {
            0,
            0,
            (Float32)ctx.swapchain.width,
            (Float32)ctx.swapchain.height,
            0,
            1,
        };
        vkCmdSetViewport(cmd, 0, 1, &v);
        VkRect2D s = {
            {0, 0},
            {ctx.swapchain.width, ctx.swapchain.height},
        };
        vkCmdSetScissor(cmd, 0, 1, &s);

        ctx.pipelineLayout = p.layout;
    }

    void pushAddress(VkCommandBuffer cmd, Uint64 gpuAddress) {
        vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(Uint64), &gpuAddress);
    }

    void draw(VkCommandBuffer cmd, Uint32 vertexCount, Uint32 instanceCount, Uint32 firstVertex, Uint32 firstInstance) {
        vkCmdDraw(cmd, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void waitIdle() {
        vkDeviceWaitIdle(ctx.device);
    }

    void shutdown() {

        for (auto& f : ctx.perFrame) f.allocator.destroy(ctx.device);

        // destroy frames
        for (Ulen i = 0; i < STATIC_LEN(ctx.perFrame); i++) {
            auto& frame = ctx.perFrame[i];
            vkDestroyCommandPool(ctx.device, frame.commandPool, nullptr);
            vkDestroySemaphore(ctx.device, frame.acquireSemaphore, nullptr);
            vkDestroySemaphore(ctx.device, frame.renderSemaphore, nullptr);
            vkDestroyFence(ctx.device, frame.fence, nullptr);
        }

        // destroy Swapchain
        ctx.swapchain.images.destroy();
        for (auto& semaphore : ctx.swapchain.presentSemaphores) {
            vkDestroySemaphore(ctx.device, semaphore, nullptr);
        }
        ctx.swapchain.presentSemaphores.destroy();
        for (auto& imageView : ctx.swapchain.imageViews) {
            vkDestroyImageView(ctx.device, imageView, nullptr);
        }
        ctx.swapchain.imageViews.destroy();
        vkDestroySwapchainKHR(ctx.device, ctx.swapchain.handle, nullptr);

        vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);

        // destroy Device
        vkDestroyDevice(ctx.device, nullptr);

        // destroy Instance
        vkDestroyDebugUtilsMessengerEXT(ctx.instance, ctx.debugMessenger, nullptr);
        vkDestroyInstance(ctx.instance, nullptr);
    }
}
