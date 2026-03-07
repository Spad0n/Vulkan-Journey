#include "volk.h"
#include "GLFW/glfw3.h"
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "ctl/array.hpp"
#include "ctl/file.hpp"
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "vk_mem_alloc.h"
#include "gpu.hpp"

using namespace ctl;

static const char* VkResultToString(VkResult result) {
    switch (result) {
    #define VK_RESULT(x) case x: return #x;
    #include "vulkan_error.inl"
    default: return "UNKNOWN_VK_RESULT";
    }
}

#include "vk_convert.cpp"

#define GPUDEBUG 1

#define vkCheck(result) checkLocation((result), __FILE__, __LINE__)
static inline void checkLocation(VkResult result, const char* filePath, Uint32 line) {
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

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugUtilsMessengerCallbackEXT(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    const char *level;
    if      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) level = "ERROR";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) level = "WARNING";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) level = "INFO";
    else level = "DEBUG";
    fprintf(stderr, "[Validation %s]: %s\n", level, pCallbackData->pMessage);
    return VK_FALSE;
}

struct AllocInfo {
    VkBuffer handle;
    VmaAllocation allocation;
    void* cpu;
    Uint64 gpu;
    Uint32 align;
    VkDeviceSize bufferSize;
    AllocationType allocType;
};

struct ShaderInfo {
    VkShaderModule shaderModule;
    Uint32 currentWorkGroupSize[3];
};

struct PipelineInfo {
    VkPipeline handle;
};

struct CommandBufferInfo {
    VkCommandBuffer handle;
    Bool usesSwapchain = false;
};

struct TextureInfo {
    VkImage handle;
    VkImageView imageView;
    VmaAllocation allocation;
    TextureFormat format;
    Uint32 width;
    Uint32 height;
};

struct SamplerInfo {
    VkSampler handle;
};

SystemAllocator sys_alloc;

struct PerFrameData {
    VkFence fence;
    VkSemaphore acquireSemaphore;
    VkCommandPool commandPool;
    Array<gpu::CommandBufferHandle> commandBuffers{sys_alloc};
    Uint32 commandBufferCount = 0;
    gpu::Arena arena{sys_alloc};
};

struct Context {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    Uint32 queueFamilyIndex;
    VkPipelineLayout commonPipelineLayoutGraphics;
    VkPipelineLayout commonPipelineLayoutCompute;
    VkQueue queue;
    VmaAllocator vmaAllocator;
    gpu::Swapchain swapchain{sys_alloc};
    Array<PerFrameData> perFrames{sys_alloc};
    Uint32 frameIndex = 0;
    Uint32 imageIndex = 0;
    VkDescriptorSetLayout textureDescriptorLayout;
    VkDescriptorSetLayout samplerDescriptorLayout;
    Uint64 textureDescriptorSize = 0;
    Uint64 samplerDescriptorSize = 0;

    gpu::ResourcePool<AllocInfo, gpu::AllocTag> allocs{sys_alloc};
    gpu::ResourcePool<CommandBufferInfo, gpu::CommandBufferTag> commandBuffers{sys_alloc};
    gpu::ResourcePool<ShaderInfo, gpu::ShaderTag> shaders{sys_alloc};
    gpu::ResourcePool<PipelineInfo, gpu::GraphicsPipelineTag> graphicsPipelines{sys_alloc};
    gpu::ResourcePool<PipelineInfo, gpu::ComputePipelineTag> computePipelines{sys_alloc};
    gpu::ResourcePool<TextureInfo, gpu::TextureTag> textures{sys_alloc};
    gpu::ResourcePool<SamplerInfo, gpu::SamplerTag> samplers{sys_alloc};
};

static Context ctx;

namespace gpu {
    static Bool getBufferAndOffset(RawPtr ptr, VkBuffer& outBuffer, VkDeviceSize& outOffset) {
        if (!ptr.handle.is_valid()) return false;

        AllocInfo* info = ctx.allocs.get(ptr.handle);
        if (!info) return false;

        outBuffer = info->handle;
        outOffset = ptr.gpu - info->gpu;
        return true;
    }

    Bool init(TemporaryAllocator& temp_alloc, GLFWwindow *window) {
        if (volkInitialize() != VK_SUCCESS) return false;

        // Instance
        {
            #if GPUDEBUG
            const char* layers[] = {
                "VK_LAYER_KHRONOS_validation",
            };
            #else
            const char* layers[] = {
            };
            #endif

            Array<const char*> extensions(temp_alloc);
            #if GPUDEBUG
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            #endif

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
                .pApplicationName = "No Graphics API",
                .apiVersion = VK_API_VERSION_1_3,
            };

            VkInstanceCreateInfo instanceCI {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                #if GPUDEBUG
                .pNext = &debugMessengerCI,
                #else
                .pNext = nullptr,
                #endif
                .pApplicationInfo = &appInfo,
                .enabledLayerCount = STATIC_LEN(layers),
                .ppEnabledLayerNames = layers,
                .enabledExtensionCount = (Uint32)extensions.length(),
                .ppEnabledExtensionNames = extensions.data(),
            };

            vkCheck(vkCreateInstance(&instanceCI, nullptr, &ctx.instance));

            volkLoadInstance(ctx.instance);
            assert(vkDestroyInstance != nullptr);

            #if GPUDEBUG
            vkCheck(vkCreateDebugUtilsMessengerEXT(ctx.instance, &debugMessengerCI, nullptr, &ctx.debugMessenger));
            #else
            ctx.debugMessenger = nullptr;
            #endif
        }

        // Creation of surface
        {
            vkCheck(glfwCreateWindowSurface(ctx.instance, window, nullptr, &ctx.surface));
        }

        // Create device
        {
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
                VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
                VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
            };

            VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
                .extendedDynamicState3DepthClampEnable = true,
                .extendedDynamicState3PolygonMode = true,
                .extendedDynamicState3ColorBlendEnable = true,
                .extendedDynamicState3ColorBlendEquation = true,
                .extendedDynamicState3ColorWriteMask = true,
                .extendedDynamicState3DepthClipEnable = true,
            };

            VkPhysicalDeviceDescriptorBufferFeaturesEXT descBufferFeatures {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
                .pNext = &eds3Features,
                .descriptorBuffer = true,
            };

            VkPhysicalDeviceVulkan13Features vulkan13Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                .pNext = &descBufferFeatures,
                .synchronization2 = true,
                .dynamicRendering = true,
            };

            VkPhysicalDeviceVulkan12Features vulkan12Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                .pNext = &vulkan13Features,
                .descriptorIndexing = true,
                .shaderSampledImageArrayNonUniformIndexing = true,
                .descriptorBindingPartiallyBound = true,
                .runtimeDescriptorArray = true,
                .scalarBlockLayout = true,
                .timelineSemaphore = true,
                .bufferDeviceAddress = true,
            };

            VkPhysicalDeviceVulkan11Features vulkan11Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                .pNext = &vulkan12Features,
                .shaderDrawParameters = true,
            };

            VkPhysicalDeviceFeatures2 physicalFeatures2 {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &vulkan11Features,
                .features = {
                    .shaderInt64 = true,
                }
            };

            VkDeviceCreateInfo deviceCI {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
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

        // Instance Vulkan Memory Allocator
        {
            VmaAllocatorCreateInfo allocatorInfo{};
            allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
            allocatorInfo.physicalDevice = ctx.physicalDevice;
            allocatorInfo.device = ctx.device;
            allocatorInfo.instance = ctx.instance;
            allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

            VmaVulkanFunctions vulkanFunctions{};
            vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);

            allocatorInfo.pVulkanFunctions = &vulkanFunctions;

            vkCheck(vmaCreateAllocator(&allocatorInfo, &ctx.vmaAllocator));
        }

        // descriptor size
        {
            VkPhysicalDeviceDescriptorBufferPropertiesEXT descBufferProps { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT };
            VkPhysicalDeviceProperties2 props2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &descBufferProps };
            vkGetPhysicalDeviceProperties2(ctx.physicalDevice, &props2);

            ctx.textureDescriptorSize = descBufferProps.sampledImageDescriptorSize;
            ctx.samplerDescriptorSize = descBufferProps.samplerDescriptorSize;

            VkDescriptorBindingFlags bindFlag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = 1,
                .pBindingFlags = &bindFlag,
            };

            // --- NOUVEAU : Création des Descriptor Set Layouts (Bindless)
            VkDescriptorSetLayoutBinding texBinding = { 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 65536, VK_SHADER_STAGE_ALL, nullptr };
            VkDescriptorSetLayoutCreateInfo texLayoutCI = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = &bindingFlagsCI,
                .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                .bindingCount = 1,
                .pBindings = &texBinding,
            };
            vkCheck(vkCreateDescriptorSetLayout(ctx.device, &texLayoutCI, nullptr, &ctx.textureDescriptorLayout));

            VkDescriptorSetLayoutBinding sampBinding = { 0, VK_DESCRIPTOR_TYPE_SAMPLER, 128, VK_SHADER_STAGE_ALL, nullptr };
            VkDescriptorSetLayoutCreateInfo sampLayoutCI = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = &bindingFlagsCI,
                .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                .bindingCount = 1,
                .pBindings = &sampBinding,
            };
            vkCheck(vkCreateDescriptorSetLayout(ctx.device, &sampLayoutCI, nullptr, &ctx.samplerDescriptorLayout));
        }

        // Pipeline Layout Global Graphics
        {
            VkPushConstantRange pushConstantRange {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = 128,
            };

            VkDescriptorSetLayout layouts[] = {
                ctx.textureDescriptorLayout,
                ctx.samplerDescriptorLayout,
            };

            VkPipelineLayoutCreateInfo pipelineLayoutCI {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = STATIC_LEN(layouts), // Zero Descriptor Sets ! All its bindless.
                .pSetLayouts = layouts,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushConstantRange,
            };

            vkCheck(vkCreatePipelineLayout(ctx.device, &pipelineLayoutCI, nullptr, &ctx.commonPipelineLayoutGraphics));
        }


        // Pipeline Layout Global Compute
        {
            VkPushConstantRange pushConstantRange {
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .offset = 0,
                .size = 128,
            };

            VkPipelineLayoutCreateInfo pipelineLayoutCI {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 0,
                .pSetLayouts = nullptr,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushConstantRange,
            };

            vkCheck(vkCreatePipelineLayout(ctx.device, &pipelineLayoutCI, nullptr, &ctx.commonPipelineLayoutCompute));
        }

        return true;
    }

    void shutdown(void) {
        destroySwapchain(ctx.swapchain);

        if (ctx.device) {
            vkDeviceWaitIdle(ctx.device);

            vkDestroyDescriptorSetLayout(ctx.device, ctx.textureDescriptorLayout, nullptr);
            vkDestroyDescriptorSetLayout(ctx.device, ctx.samplerDescriptorLayout, nullptr);

            for (auto& frame : ctx.perFrames) {
                vkDestroyFence(ctx.device, frame.fence, nullptr);
                vkDestroySemaphore(ctx.device, frame.acquireSemaphore, nullptr);
                vkDestroyCommandPool(ctx.device, frame.commandPool, nullptr);
                frame.arena.destroy();
                frame.commandBuffers.destroy();
            }
            ctx.perFrames.destroy();

            if (ctx.vmaAllocator) {
                vmaDestroyAllocator(ctx.vmaAllocator);
            }
            vkDestroyPipelineLayout(ctx.device, ctx.commonPipelineLayoutGraphics, nullptr);
            vkDestroyPipelineLayout(ctx.device, ctx.commonPipelineLayoutCompute, nullptr);
            vkDestroyDevice(ctx.device, nullptr);
        }

        if (ctx.surface) {
            vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
        }

        if (ctx.debugMessenger) {
            vkDestroyDebugUtilsMessengerEXT(ctx.instance, ctx.debugMessenger, nullptr);
        }

        if (ctx.instance) {
            vkDestroyInstance(ctx.instance, nullptr);
        }

        ctx.allocs.destroy();
        ctx.commandBuffers.destroy();
        ctx.shaders.destroy();
        ctx.graphicsPipelines.destroy();
        ctx.computePipelines.destroy();
        ctx.textures.destroy();
        ctx.samplers.destroy();
    }

    void waitIdle(void) {
        vkDeviceWaitIdle(ctx.device);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// SWAPCHAIN ////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////
    Swapchain createSwapchain(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height) {
        Swapchain res{sys_alloc};

        VkSurfaceCapabilitiesKHR surfaceCaps{0};
        vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));

        Uint32 imageCount = surfaceCaps.minImageCount + 1;
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
        res.imageFormat = surfaceFormat.format;

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

        res.width = width;
        res.height = height;

        VkSwapchainCreateInfoKHR swapchainCI = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = ctx.surface,
            .minImageCount = imageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = {
                .width  = res.width,
                .height = res.height,
            },
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = surfaceCaps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = true
        };
        vkCheck(vkCreateSwapchainKHR(ctx.device, &swapchainCI, nullptr, &res.handle));

        vkCheck(vkGetSwapchainImagesKHR(ctx.device, res.handle, &imageCount, nullptr));
        res.images.resize(imageCount);
        vkCheck(vkGetSwapchainImagesKHR(ctx.device, res.handle, &imageCount, res.images.data()));

        res.imageViews.resize(imageCount);
        res.presentSemaphores.resize(imageCount);
        for (Ulen i = 0; i < res.images.length(); i++) {
            VkImageViewCreateInfo imageCI {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = res.images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surfaceFormat.format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            };
            vkCheck(vkCreateImageView(ctx.device, &imageCI, nullptr, &res.imageViews[i]));

            VkSemaphoreCreateInfo semaphoreCI { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &res.presentSemaphores[i]));
        }

        return res;
    }

    void destroySwapchain(Swapchain& swapchain) {
        swapchain.images.destroy();
        for (auto semaphore : swapchain.presentSemaphores) {
            vkDestroySemaphore(ctx.device, semaphore, nullptr);
        }
        swapchain.presentSemaphores.destroy();
        for (auto imageView : swapchain.imageViews) {
            vkDestroyImageView(ctx.device, imageView, nullptr);
        }
        swapchain.imageViews.destroy();
        vkDestroySwapchainKHR(ctx.device, swapchain.handle, nullptr);
    }

    void swapchainInit(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height, Uint32 framesInFlight) {
        ctx.swapchain = createSwapchain(temp_alloc, width, height);

        // instance frames
        ctx.perFrames.resize(framesInFlight);
        VkCommandPoolCreateInfo commandPoolCI {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx.queueFamilyIndex,
        };
        for (auto& frame : ctx.perFrames) {
            vkCheck(vkCreateCommandPool(ctx.device, &commandPoolCI, nullptr, &frame.commandPool));

            frame.commandBufferCount = 0;

            VkSemaphoreCreateInfo semaphoreCI { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &frame.acquireSemaphore));

            VkFenceCreateInfo fenceCI {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            vkCheck(vkCreateFence(ctx.device, &fenceCI, nullptr, &frame.fence));
        }
    }

    void recreateSwapchain(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height) {
        vkDeviceWaitIdle(ctx.device);

        VkSwapchainKHR oldSwapchain = ctx.swapchain.handle;

        // reset ctx.swapchain
        for (auto semaphore : ctx.swapchain.presentSemaphores) {
            vkDestroySemaphore(ctx.device, semaphore, nullptr);
        }
        ctx.swapchain.presentSemaphores.reset();
        for (auto imageView : ctx.swapchain.imageViews) {
            vkDestroyImageView(ctx.device, imageView, nullptr);
        }
        ctx.swapchain.imageViews.reset();
        ctx.swapchain.images.reset();

        // recreate swapchain
        VkSurfaceCapabilitiesKHR surfaceCaps{0};
        vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));

        Uint32 imageCount = surfaceCaps.minImageCount + 1;
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
            .clipped = true,
            .oldSwapchain = oldSwapchain,
        };
        vkCheck(vkCreateSwapchainKHR(ctx.device, &swapchainCI, nullptr, &ctx.swapchain.handle));

        if (oldSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(ctx.device, oldSwapchain, nullptr);
        }

        vkCheck(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain.handle, &imageCount, nullptr));
        ctx.swapchain.images.resize(imageCount);
        vkCheck(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain.handle, &imageCount, ctx.swapchain.images.data()));

        ctx.swapchain.imageViews.resize(imageCount);
        ctx.swapchain.presentSemaphores.resize(imageCount);

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

            VkSemaphoreCreateInfo semaphoreCI { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &ctx.swapchain.presentSemaphores[i]));
        }
    }

    Arena& getFrameArena() {
        return ctx.perFrames[ctx.frameIndex].arena;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// SHADER ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////
    static ShaderHandle shaderCreateInternal(Slice<Uint8> shaderCode, Uint32 groupSizeX = 1, Uint32 groupSizeY = 1, Uint32 groupSizeZ = 1) {
        VkShaderModuleCreateInfo moduleCI {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shaderCode.length(),
            .pCode = (Uint32*)shaderCode.data(),
        };
        VkShaderModule vkModule{0};
        vkCheck(vkCreateShaderModule(ctx.device, &moduleCI, nullptr, &vkModule));

        ShaderInfo shader {
            .shaderModule = vkModule,
            .currentWorkGroupSize = { groupSizeX, groupSizeY, groupSizeZ },
        };

        return ctx.shaders.add(shader).value();
    }

    ShaderHandle shaderCreate(TemporaryAllocator& temp_alloc, StringView filePath) {
        Array<Uint8> shaderCode = loadFile(temp_alloc, filePath);
        return shaderCreateInternal(shaderCode.slice());
    }

    void shaderDestroy(ShaderHandle shader) {
        auto shaderInfo = ctx.shaders.get(shader);
        vkDestroyShaderModule(ctx.device, shaderInfo->shaderModule, nullptr);
        ctx.shaders.remove(shader);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// MEMORY MANAGMENT /////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////
    RawPtr memAllocRaw(Uint64 elSize, Uint64 elCount, Uint64 align, Memory memType, AllocationType allocType) {
        auto bytes = elSize * elCount;

        VmaMemoryUsage vmaUsage;
        VkMemoryPropertyFlags properties{};
        switch (memType) {
        case Memory::Default:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            vmaUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            break;
        case Memory::GPU:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            vmaUsage = VMA_MEMORY_USAGE_GPU_ONLY;
            break;
        case Memory::Readback:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            vmaUsage = VMA_MEMORY_USAGE_GPU_TO_CPU;
            break;
        }

        VkBufferUsageFlags bufUsage{};
        switch (allocType) {
        case AllocationType::Default:
            bufUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            if (memType == Memory::GPU) {
                bufUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            }
            // TODO: raytracing part
            break;
        case AllocationType::TextureDescriptor:
            bufUsage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;

        case AllocationType::SamplerDescriptor:
            bufUsage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        }

        VkBufferCreateInfo bufferCI {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = (VkDeviceSize)bytes,
            .usage = bufUsage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VkBuffer buffer;
        vkCheck(vkCreateBuffer(ctx.device, &bufferCI, nullptr, &buffer));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx.device, buffer, &memRequirements);

        memRequirements.alignment = VkDeviceSize(MAX(memRequirements.alignment, align));

        VmaAllocationCreateInfo allocCI {
            .flags = memType != Memory::GPU ? VMA_ALLOCATION_CREATE_MAPPED_BIT : (Uint32)0,
            .usage = vmaUsage,
            .requiredFlags = properties,
        };

        VmaAllocation alloc;
        VmaAllocationInfo vmaAllocInfo;
        vkCheck(vmaAllocateMemory(ctx.vmaAllocator, &memRequirements, &allocCI, &alloc, &vmaAllocInfo));

        vkCheck(vmaBindBufferMemory(ctx.vmaAllocator, alloc, buffer));

        RawPtr p;
        if (memType != Memory::GPU) {
            p.cpu = vmaAllocInfo.pMappedData;
        }

        VkBufferDeviceAddressInfo info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer,
        };
        auto addr = vkGetBufferDeviceAddress(ctx.device, &info);
        p.gpu = addr;

        AllocInfo allocInfo {
            .handle = buffer,
            .allocation = alloc,
            .cpu = p.cpu,
            .gpu = p.gpu,
            .align = (Uint32)align,
            .bufferSize = (VkDeviceSize)bytes,
            .allocType = allocType,
        };
        p.handle = ctx.allocs.add(allocInfo).value();
        return p;
    }

    void memFreeRaw(RawPtr& ptr) {
        auto allocInfo = ctx.allocs.get(ptr.handle);
        vmaDestroyBuffer(ctx.vmaAllocator, allocInfo->handle, allocInfo->allocation);
        ctx.allocs.remove(ptr.handle);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// GRAPHICS PIPELINE ////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////
    GraphicsPipelineHandle graphicsPipelineCreate(TemporaryAllocator& temp_alloc, PipelineDesc desc) {
        // 1. Shaders Stages
        Array<VkPipelineShaderStageCreateInfo> stages(temp_alloc);

        auto vsInfo = ctx.shaders.get(desc.vs);
        VkPipelineShaderStageCreateInfo shaderStageVertCI {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vsInfo->shaderModule,
            .pName = "main",
        };
        stages.push_back(shaderStageVertCI);

        auto fsInfo = ctx.shaders.get(desc.fs);
        VkPipelineShaderStageCreateInfo shaderStageFragCI {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fsInfo->shaderModule,
            .pName = "main",
        };
        stages.push_back(shaderStageFragCI);

        // 2. Dynamic Rendering (Formats)
        Array<VkFormat> colorFormats(temp_alloc);
        colorFormats.resize(desc.colorFormats.length());
        for (Ulen i = 0; i < desc.colorFormats.length(); i++) {
            colorFormats[i] = toVkTextureFormat(desc.colorFormats[i]);
        }

        VkPipelineRenderingCreateInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = (Uint32)colorFormats.length(),
            .pColorAttachmentFormats = colorFormats.data(),
            .depthAttachmentFormat = desc.depthFormat == TextureFormat::Default ? VK_FORMAT_UNDEFINED : toVkTextureFormat(desc.depthFormat),
            .stencilAttachmentFormat = desc.stencilFormat == TextureFormat::Default ? VK_FORMAT_UNDEFINED : toVkTextureFormat(desc.stencilFormat),
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = false,
        };

        VkPipelineMultisampleStateCreateInfo multisample {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        };

        VkPipelineColorBlendStateCreateInfo colorBlend {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        };

        VkPipelineVertexInputStateCreateInfo vertexInput {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        VkPipelineViewportStateCreateInfo viewportState {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .lineWidth = 1.0,
        };

        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,

            // Core Vulkan 1.3 (from EDS1 and EDS2)
            VK_DYNAMIC_STATE_CULL_MODE,
            VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            
            // Extended Dynamic State 3
            VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
            VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
            VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
        };

        VkPipelineDynamicStateCreateInfo dynamicInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = (Uint32)STATIC_LEN(dynamicStates),
            .pDynamicStates = dynamicStates,
        };

        VkGraphicsPipelineCreateInfo pipelineCI {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingInfo,
            .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .stageCount = (Uint32)stages.length(),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssembly, // ignored thanks to dynamic state
            .pViewportState = &viewportState,      // ignored
            .pRasterizationState = &rasterizer,    // partially ignored
            .pMultisampleState = &multisample,
            .pDepthStencilState = &depthStencil,   // ignored
            .pColorBlendState = &colorBlend,       // ignored
            .pDynamicState = &dynamicInfo,         
            .layout = ctx.commonPipelineLayoutGraphics,
        };

        VkPipeline pipeline{0};
        vkCheck(vkCreateGraphicsPipelines(ctx.device, 0, 1, &pipelineCI, nullptr, &pipeline));

        PipelineInfo gpInfo {
            .handle = pipeline,
        };
        return ctx.graphicsPipelines.add(gpInfo).value();
    }

    void graphicsPipelineDestroy(GraphicsPipelineHandle pipeline) {
        auto info = ctx.graphicsPipelines.get(pipeline);
        vkDestroyPipeline(ctx.device, info->handle, nullptr);
        ctx.graphicsPipelines.remove(pipeline);
    }

    ComputePipelineHandle computePipelineCreate(TemporaryAllocator& temp_alloc, ShaderHandle cs) {
        auto csInfo = ctx.shaders.get(cs);

        VkComputePipelineCreateInfo pipelineCI {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = csInfo->shaderModule,
                .pName = "main",
            },
            .layout = ctx.commonPipelineLayoutCompute,
        };

        VkPipeline pipeline{0};
        vkCheck(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));

        return ctx.computePipelines.add(PipelineInfo{pipeline}).value();
    }

    void computePipelineDestroy(ComputePipelineHandle pipeline) {
        auto info = ctx.computePipelines.get(pipeline);
        vkDestroyPipeline(ctx.device, info->handle, nullptr);
        ctx.computePipelines.remove(pipeline);
    }

    void cmdBindComputePipeline(CommandBufferHandle cmd, ComputePipelineHandle pipeline) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        auto pInfo = ctx.computePipelines.get(pipeline);
        if (cbInfo && pInfo) {
            vkCmdBindPipeline(cbInfo->handle, VK_PIPELINE_BIND_POINT_COMPUTE, pInfo->handle);
        }
    }

    void cmdDispatch(CommandBufferHandle cmd, Uint32 groupCountX, Uint32 groupCountY, Uint32 groupCountZ) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        vkCmdDispatch(cbInfo->handle, groupCountX, groupCountY, groupCountZ);
    }

    void cmdPushConstantsCompute(CommandBufferHandle cmd, const void* data, Uint32 size) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        vkCmdPushConstants(cbInfo->handle, ctx.commonPipelineLayoutCompute, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// ARENA ////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////
    static constexpr Uint64 alignUp(Uint64 value, Uint64 alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void Arena::destroy() {
        for (auto& block : blocks_) {
            memFreeRaw(block.ptr);
        }
        blocks_.destroy();
    }

    void Arena::reset() {
        currentOffset_ = 0;
        currentBlockIdx_ = 0;
    }

    RawPtr Arena::allocRaw(Uint64 sizeType, Uint64 count, Uint64 alignment) {
        auto size = sizeType * count;
        if (blocks_.is_empty()) {
            blocks_.push_back(allocateBlock(MAX(blockSize_, size)));
        }

        Uint64 alignedOffset = alignUp(currentOffset_, alignment);

        if (alignedOffset + size > blocks_[currentBlockIdx_].size) {
            currentBlockIdx_++;
            currentOffset_ = 0;
            alignedOffset = 0;

            if (currentBlockIdx_ >= blocks_.length()) {
                blocks_.push_back(allocateBlock(MAX(blockSize_, size)));
            }
        }

        Block& block = blocks_[currentBlockIdx_];

        RawPtr ptr;
        if (block.ptr.cpu) {
            ptr.cpu = static_cast<Uint8*>(block.ptr.cpu) + alignedOffset;
        } else {
            ptr.cpu = nullptr; // Case where Memory::GPU is used (not mappable on the CPU)
        }
        ptr.gpu = block.ptr.gpu + alignedOffset;
        
        ptr.handle = block.ptr.handle; 

        currentOffset_ = alignedOffset + size;

        return ptr;
    }

    Arena::Block Arena::allocateBlock(Uint64 requiredSize) {
        RawPtr raw = memAllocRaw(requiredSize, 1, 16, memType_, AllocationType::Default);
        return Block { raw, requiredSize };
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// TEXTURES /////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////

    TextureHandle textureCreate(Uint32 width, Uint32 height, TextureFormat format) {
        VkFormat vkFormat = toVkTextureFormat(format);
        VkImageCreateInfo imageCI {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = vkFormat,
            .extent = { width, height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo allocCI = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };

        VkImage image;
        VmaAllocation allocation;
        vkCheck(vmaCreateImage(ctx.vmaAllocator, &imageCI, &allocCI, &image, &allocation, nullptr));

        VkImageViewCreateInfo viewCI {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vkFormat,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        
        VkImageView imageView;
        vkCheck(vkCreateImageView(ctx.device, &viewCI, nullptr, &imageView));

        return ctx.textures.add(TextureInfo{
            image,
            imageView,
            allocation,
            format,
            width,
            height,
        }).value();
    }

    void textureDestroy(TextureHandle texture) {
        auto info = ctx.textures.get(texture);
        vkDestroyImageView(ctx.device, info->imageView, nullptr);
        vmaDestroyImage(ctx.vmaAllocator, info->handle, info->allocation);
    }

    SamplerHandle samplerCreate(void) {
        VkSamplerCreateInfo samplerCI {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxAnisotropy = 1.0f,
        };
        VkSampler sampler;
        vkCheck(vkCreateSampler(ctx.device, &samplerCI, nullptr, &sampler));
        return ctx.samplers.add(SamplerInfo{sampler}).value();
    }

    void samplerDestroy(SamplerHandle sampler) {
        auto info = ctx.samplers.get(sampler);
        vkDestroySampler(ctx.device, info->handle, nullptr);
        ctx.samplers.remove(sampler);
    }

    void cmdCopyToTexture(CommandBufferHandle cmd, TextureHandle texture, RawPtr srcBuffer) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        auto texInfo = ctx.textures.get(texture);

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        if (!getBufferAndOffset(srcBuffer, buf, offset)) return;

        VkImageMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = texInfo->handle,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };

        VkDependencyInfo depInfo { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
        vkCmdPipelineBarrier2(cbInfo->handle, &depInfo);

        VkBufferImageCopy copy = {};
        copy.bufferOffset = offset;
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { texInfo->width, texInfo->height, 1 };
        vkCmdCopyBufferToImage(cbInfo->handle, buf, texInfo->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(cbInfo->handle, &depInfo);
    }

    Uint64 getTextureDescriptorSize(void) {
        return ctx.textureDescriptorSize;
    }

    Uint64 getSamplerDescriptorSize(void) {
        return ctx.samplerDescriptorSize;
    }

    void writeTextureDescriptor(RawPtr heap, Uint32 index, TextureHandle tex) {
        auto texInfo = ctx.textures.get(tex);
        VkDescriptorImageInfo imageInfo { VK_NULL_HANDLE, texInfo->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        
        // Initialisation explicite et propre
        VkDescriptorGetInfoEXT getInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .pNext = nullptr, // On met explicitement NULL ici
            .type  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .data  = { .pSampledImage = &imageInfo }
        };
        
        void* ptr = static_cast<Uint8*>(heap.cpu) + (index * ctx.textureDescriptorSize);
        vkGetDescriptorEXT(ctx.device, &getInfo, ctx.textureDescriptorSize, ptr);
    }

    void writeSamplerDescriptor(RawPtr heap, Uint32 index, SamplerHandle sampler) {
        auto sampInfo = ctx.samplers.get(sampler);
        
        VkDescriptorGetInfoEXT getInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .pNext = nullptr,
            .type  = VK_DESCRIPTOR_TYPE_SAMPLER,
            .data  = { .pSampler = &sampInfo->handle }
        };
        
        void* ptr = static_cast<Uint8*>(heap.cpu) + (index * ctx.samplerDescriptorSize);
        vkGetDescriptorEXT(ctx.device, &getInfo, ctx.samplerDescriptorSize, ptr);
    }

    void cmdBindDescriptorHeaps(CommandBufferHandle cmd, RawPtr textureHeap, RawPtr samplerHeap) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        
        VkDescriptorBufferBindingInfoEXT bindings[2] = {};
        bindings[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        bindings[0].address = textureHeap.gpu;
        bindings[0].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
        
        bindings[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        bindings[1].address = samplerHeap.gpu;
        bindings[1].usage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

        vkCmdBindDescriptorBuffersEXT(cbInfo->handle, 2, bindings);

        Uint32 bufferIndices[2] = { 0, 1 };
        VkDeviceSize offsets[2] = { 0, 0 };
        vkCmdSetDescriptorBufferOffsetsEXT(cbInfo->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.commonPipelineLayoutGraphics, 0, 2, bufferIndices, offsets);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //////// COMMANDS BUFFERS /////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////

    CommandBufferHandle commandsBegin(void) {
        auto& frame = ctx.perFrames[ctx.frameIndex];
        VkCommandBuffer cb = VK_NULL_HANDLE;
        CommandBufferHandle handle;

        if (frame.commandBufferCount < frame.commandBuffers.length()) {
            handle = frame.commandBuffers[frame.commandBufferCount];
            auto cbInfo = ctx.commandBuffers.get(handle);
            cbInfo->usesSwapchain = false;
            cb = cbInfo->handle;
        } else {
            // S'il nous en faut un nouveau, on l'alloue et on le cache
            VkCommandBufferAllocateInfo allocInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = frame.commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            vkCheck(vkAllocateCommandBuffers(ctx.device, &allocInfo, &cb));

            CommandBufferInfo cbInfo { cb, false };
            handle = ctx.commandBuffers.add(cbInfo).value();
            frame.commandBuffers.push_back(handle);
        }

        frame.commandBufferCount += 1;

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(cb, &beginInfo));

        return handle;
    }

    void queueSubmit(CommandBufferHandle cmd) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        vkCheck(vkEndCommandBuffer(cbInfo->handle));

        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cbInfo->handle,
        };

        auto& frame = ctx.perFrames[ctx.frameIndex];
        VkFence fenceToSignal = VK_NULL_HANDLE;

        // C'est ici que la magie de la V1 s'opère :
        if (cbInfo->usesSwapchain) {
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &frame.acquireSemaphore;
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &ctx.swapchain.presentSemaphores[ctx.imageIndex];

            fenceToSignal = frame.fence;
        }

        vkCheck(vkQueueSubmit(ctx.queue, 1, &submitInfo, fenceToSignal));
    }

    Bool acquireNextImage(Uint32& outWidth, Uint32& outHeight) {
        auto& frame = ctx.perFrames[ctx.frameIndex];

        vkCheck(vkWaitForFences(ctx.device, 1, &frame.fence, VK_TRUE, UINT64_MAX));

        VkResult res = vkAcquireNextImageKHR(ctx.device, ctx.swapchain.handle, UINT64_MAX, frame.acquireSemaphore, VK_NULL_HANDLE, &ctx.imageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            return false;
        }
        
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
            vkCheck(res);
        }

        vkCheck(vkResetFences(ctx.device, 1, &frame.fence));
        frame.commandBufferCount = 0;
        frame.arena.reset();

        outWidth = ctx.swapchain.width;
        outHeight = ctx.swapchain.height;
        return true;
    }

    void present(void) {
        VkSemaphore presentSemaphore = ctx.swapchain.presentSemaphores[ctx.imageIndex];

        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &ctx.swapchain.handle,
            .pImageIndices = &ctx.imageIndex,
        };
        vkQueuePresentKHR(ctx.queue, &presentInfo);

        ctx.frameIndex = (ctx.frameIndex + 1) % ctx.perFrames.length();
    }

    void cmdBeginRendering(CommandBufferHandle cmd, LoadOp loadOp, StoreOp storeOp, Float32 r, Float32 g, Float32 b, Float32 a) {
        auto cbInfo = ctx.commandBuffers.get(cmd);

        cbInfo->usesSwapchain = true;

        // Transition from image Swapchain to COLOR_ATTACHMENT
        VkImageMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = ctx.swapchain.images[ctx.imageIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        VkDependencyInfo depInfo { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
        vkCmdPipelineBarrier2(cbInfo->handle, &depInfo);

        VkRenderingAttachmentInfo colorAtt {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = ctx.swapchain.imageViews[ctx.imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = loadOp == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = storeOp == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = { .color = { .float32 = {r, g, b, a} } }
        };

        VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { {0, 0}, {ctx.swapchain.width, ctx.swapchain.height} },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAtt,
        };
        vkCmdBeginRendering(cbInfo->handle, &renderingInfo);
    }

    void cmdEndRendering(CommandBufferHandle cmd) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        vkCmdEndRendering(cbInfo->handle);

        // Transition to PRESENT
        VkImageMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = ctx.swapchain.images[ctx.imageIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        VkDependencyInfo depInfo { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
        vkCmdPipelineBarrier2(cbInfo->handle, &depInfo);
    }

    void cmdSetViewportScissor(CommandBufferHandle cmd, Uint32 width, Uint32 height) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        VkViewport v { 0.0f, 0.0f, (Float32)width, (Float32)height, 0.0f, 1.0f };
        VkRect2D s { {0, 0}, {width, height} };
        vkCmdSetViewport(cbInfo->handle, 0, 1, &v);
        vkCmdSetScissor(cbInfo->handle, 0, 1, &s);
    }

    void cmdPushConstants(CommandBufferHandle cmd, const void* data, Uint32 size) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        vkCmdPushConstants(cbInfo->handle, ctx.commonPipelineLayoutGraphics, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
    }


    void cmdMemCpy(CommandBufferHandle cmd, RawPtr dst, RawPtr src, Uint64 bytes) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        VkBuffer srcBuf = VK_NULL_HANDLE, dstBuf = VK_NULL_HANDLE;
        VkDeviceSize srcOffset = 0, dstOffset = 0;

        if (getBufferAndOffset(src, srcBuf, srcOffset) && getBufferAndOffset(dst, dstBuf, dstOffset)) {
            VkBufferCopy copyRegion {
                .srcOffset = srcOffset,
                .dstOffset = dstOffset,
                .size = bytes,
            };
            vkCmdCopyBuffer(cbInfo->handle, srcBuf, dstBuf, 1, &copyRegion);
        }
    }

    void cmdBindGraphicsPipeline(CommandBufferHandle cmd, GraphicsPipelineHandle pipeline) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        auto pInfo = ctx.graphicsPipelines.get(pipeline);
        if (cbInfo && pInfo) {
            vkCmdBindPipeline(cbInfo->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pInfo->handle);
        }
    }

    void cmdSetBlendState(CommandBufferHandle cmd, const BlendState& state) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        VkBool32 blendEnable = state.enable ? VK_TRUE : VK_FALSE;
        vkCmdSetColorBlendEnableEXT(cbInfo->handle, 0, 1, &blendEnable);

        if (state.enable) {
            VkColorBlendEquationEXT equation {
                .srcColorBlendFactor = toVkBlendFactor(state.srcColorFactor),
                .dstColorBlendFactor = toVkBlendFactor(state.dstColorFactor),
                .colorBlendOp = toVkBlendOp(state.colorOp),
                .srcAlphaBlendFactor = toVkBlendFactor(state.srcAlphaFactor),
                .dstAlphaBlendFactor = toVkBlendFactor(state.dstAlphaFactor),
                .alphaBlendOp = toVkBlendOp(state.alphaOp),
            };
            vkCmdSetColorBlendEquationEXT(cbInfo->handle, 0, 1, &equation);
        }

        VkColorComponentFlags mask = state.colorWriteMask;
        if (mask == 0) {
            mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        vkCmdSetColorWriteMaskEXT(cbInfo->handle, 0, 1, &mask);
    }

    void cmdSetDepthState(CommandBufferHandle cmd, const DepthState& state) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        vkCmdSetDepthTestEnable(cbInfo->handle, (state.mode & DepthFlags::Read) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(cbInfo->handle, (state.mode & DepthFlags::Write) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(cbInfo->handle, toVkCompareOp(state.compare));
    }

    void cmdSetRasterizerState(CommandBufferHandle cmd, CullMode cull, FrontFace face, PrimitiveTopology topology) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        vkCmdSetCullMode(cbInfo->handle, toVkCullMode(cull));
        vkCmdSetFrontFace(cbInfo->handle, toVkFrontFace(face));
        vkCmdSetPrimitiveTopology(cbInfo->handle, toVkTopology(topology));
    }

    void cmdBarrier(CommandBufferHandle cmd, Stage before, Stage after, Hazard hazards) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        VkMemoryBarrier2 barrier { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = toVkStage(before);
        barrier.dstStageMask = toVkStage(after);

        if (hazards & Hazard::DrawArguments) {
            barrier.srcAccessMask |= VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }
        if (hazards & Hazard::Descriptors) {
            barrier.srcAccessMask |= VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
        }
        if (hazards & Hazard::DepthStencil) {
            barrier.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        if (hazards == Hazard::None) {
            barrier.srcAccessMask |= VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.dstAccessMask |= VK_ACCESS_2_MEMORY_READ_BIT;
        }

        VkDependencyInfo depInfo { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.memoryBarrierCount = 1;
        depInfo.pMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cbInfo->handle, &depInfo);
    }

    struct GraphicsPushConstants {
        Uint64 vertexData;
        Uint64 fragmentData;
        Uint64 indirectData;
    };

    void cmdDrawIndexedInstanced(CommandBufferHandle cmd, RawPtr vertexData, RawPtr fragmentData, RawPtr indices, Uint32 indexCount, Uint32 instanceCount) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        GraphicsPushConstants pc {
            .vertexData = vertexData.gpu,
            .fragmentData = fragmentData.gpu,
            .indirectData = 0,
        };
        vkCmdPushConstants(cbInfo->handle, ctx.commonPipelineLayoutGraphics, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GraphicsPushConstants), &pc);

        VkBuffer idxBuffer;
        VkDeviceSize idxOffset;
        if (getBufferAndOffset(indices, idxBuffer, idxOffset)) {
            vkCmdBindIndexBuffer(cbInfo->handle, idxBuffer, idxOffset, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cbInfo->handle, indexCount, instanceCount, 0, 0, 0);
        } else {
            vkCmdDraw(cbInfo->handle, indexCount, instanceCount, 0, 0);
        }
    }
}
