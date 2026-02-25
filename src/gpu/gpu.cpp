#include "volk.h"
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "gpu.hpp"
#include "../macro_utils.hpp"
#include "ctl/array.hpp"
#include "ctl/file.hpp"
#include "vk_mem_alloc.h"
#include "priority_queue.hpp"

using namespace ctl;

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

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugUtilsMessengerCallbackEXT(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    const char *level;
    if      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) level = "ERROR";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) level = "WARNING";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) level = "INFO";
    else level = "DEBUG";
    fprintf(stderr, "[Validation %s]: %s\n", level, pCallbackData->pMessage);
    return VK_FALSE;
}

static VkImageType toVkTextureType(TextureType type) {
    switch (type) {
    case TextureType::D1: return VK_IMAGE_TYPE_1D;
    case TextureType::D2: return VK_IMAGE_TYPE_2D;
    case TextureType::D3: return VK_IMAGE_TYPE_3D;
    }
}

static VkFormat toVkTextureFormat(TextureFormat type) {
    switch (type) {
    case TextureFormat::RGBA8_Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::BGRA8_Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::D32_Float: return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::BC1_RGBA_Unorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::BC3_RGBA_Unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::BC7_RGBA_Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
    case TextureFormat::ASTC_4x4_RGBA_Unorm: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case TextureFormat::ETC2_RGB8_Unorm: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
    case TextureFormat::ETC2_RGBA8_Unorm: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
    case TextureFormat::EAC_R11_Unorm: return VK_FORMAT_EAC_R11_UNORM_BLOCK;
    case TextureFormat::EAC_RG11_Unorm: return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
    default:
        fprintf(stderr, "Implementation bug\n");
        exit(1);
    }
}

static VkPrimitiveTopology toVkTopology(PrimitiveTopology type) {
    switch (type) {
    case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    default: assert(0 && "Unreachable");
    }
}

static VkCullModeFlags toVkCullMode(CullMode type) {
    switch (type) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    default: assert(0 && "Unreachable");
    }
}

static VkFrontFace toVkFrontFace(FrontFace type) {
    switch (type) {
    case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
    default: assert(0 && "Unreachable");
    }
}

static VkCompareOp toVkCompareOp(CompareOp type) {
    switch (type) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    default: assert(0 && "Unreachable");
    }
}

static VkBlendFactor toVkBlendFactor(BlendFactor type) {
    switch (type) {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    default: assert(0 && "Unreachable");
    }
}

static VkBlendOp toVkBlendOp(BlendOp type) {
    switch (type) {
    case BlendOp::Add: return VK_BLEND_OP_ADD;
    case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case BlendOp::Rev_Subtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min: return VK_BLEND_OP_MIN;
    case BlendOp::Max: return VK_BLEND_OP_MAX;
    default: assert(0 && "Unreachable");
    }
}

static VkPipelineStageFlags2 toVkStage(Stage stage) {
    switch(stage) {
    case Stage::Transfer: return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    case Stage::Compute: return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    case Stage::RasterColorOut: return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case Stage::FragmentShader: return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case Stage::VertexShader: return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    case Stage::BuildBVH: return VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    case Stage::All: return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    default: return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
}

SystemAllocator sys_alloc;

struct ImageViewInfo {
    VkImageViewCreateInfo info;
    VkImageView view;
};

struct TextureInfo {
    VkImage handle;
    Array<ImageViewInfo> views{sys_alloc};
};

struct ShaderInfo {
    VkShaderModule shaderModule;
    StringView entryPoint;
    Uint32 currentWorkgroupSize[3];
    Bool isCompute;
};

struct GraphicsPipelineInfo {
    VkPipeline handle;
};

struct AllocInfo {
    VkBuffer bufferHandle;
    VmaAllocation allocation;
    void *cpu;
    Uint64 gpu;
    Uint32 align;
    VkDeviceSize bufferSize;
    AllocationType allocType;
};

struct SemaphoreValue {
    gpu::SemaphoreHandle sem;
    Uint64 value;
};

struct CommandBufferInfo {
    VkCommandBuffer handle;
    Queue queue;
    Bool recording;

    SemaphoreValue waitSems[4];
    Uint32 numWaitSems = 0;

    SemaphoreValue signalSems[4];
    Uint32 numSignalSems = 0;
};

struct FreeCommandBuffer {
    gpu::CommandBuffer handle;
    Uint64 timelineValue;
};

struct FreeCommandBufferLess {
    constexpr Bool operator()(const FreeCommandBuffer& a, const FreeCommandBuffer& b) const {
        return a.timelineValue < b.timelineValue;
    }
};

struct Context {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    Uint32 queueFamilyIndex;
    VkDevice device;
    VkQueue queue;
    VmaAllocator vmaAllocator;
    VkCommandPool transientCmdPool;

    VkCommandPool mainCmdPool;
    PriorityQueue<FreeCommandBuffer, FreeCommandBufferLess> freeCommandBuffers{sys_alloc};
    VkSemaphore queueTimelineSem = VK_NULL_HANDLE;
    Uint64 queueTimelineValue = 0;

    Uint32 framesInFlight;
    gpu::Swapchain swapchain{sys_alloc};
    gpu::ResourcePool<TextureInfo, gpu::TextureTag> textures{sys_alloc};
    gpu::ResourcePool<ShaderInfo, gpu::ShaderTag> shaders{sys_alloc};
    gpu::ResourcePool<GraphicsPipelineInfo, gpu::PipelineTag> graphicsPipelines{sys_alloc};
    gpu::ResourcePool<AllocInfo, gpu::AllocTag> allocs{sys_alloc};
    gpu::ResourcePool<CommandBufferInfo, gpu::CommandBufferTag> commandBuffers{sys_alloc};
    gpu::ResourcePool<VkSemaphore, gpu::SemaphoreTag> semaphores{sys_alloc};
    VkPipelineLayout commonPipelineLayoutGraphics;
};

static Context ctx;

namespace gpu {

    Bool init(TemporaryAllocator& temp_alloc, GLFWwindow* window) {
        if (volkInitialize() != VK_SUCCESS) return false;

        // 1. Creation de l'Instance
        {
            const char* layers[] = {
                "VK_LAYER_KHRONOS_validation",
            };

            Array<const char*> extensions(temp_alloc);
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
                .pApplicationName = "No Graphics API C++",
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
            vkCheck(vkCreateDebugUtilsMessengerEXT(ctx.instance, &debugMessengerCI, nullptr, &ctx.debugMessenger));
        }
        
        // 2. CORRECTION : Creation de la Surface AVANT de chercher le Physical Device
        {
            vkCheck(glfwCreateWindowSurface(ctx.instance, window, nullptr, &ctx.surface));
        }
        
        // 3. Choix du GPU (Physical Device)
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
                    
                    // La surface est maintenant valide ici !
                    Uint32 supportsPresents = 0;
                    vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, ctx.surface, &supportsPresents));

                    if (supportsGraphics && supportsPresents) {
                        ctx.physicalDevice = candidate;
                        ctx.queueFamilyIndex = i;
                        goto deviceLoop;
                    }
                }
            }
        deviceLoop:
            assert(ctx.physicalDevice != nullptr && "No suitable GPU found");
        }

        // 4. Creation du Device Logique
        {
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
            vkGetDeviceQueue(ctx.device, ctx.queueFamilyIndex, 0, &ctx.queue);
        }

        // Create Vulkan Memory Allocator
        {
            VmaAllocatorCreateInfo allocatorInfo = {};
            allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
            allocatorInfo.physicalDevice = ctx.physicalDevice;
            allocatorInfo.device = ctx.device;
            allocatorInfo.instance = ctx.instance;
            allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; 

            VmaVulkanFunctions vulkanFunctions = {};

            vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);

            allocatorInfo.pVulkanFunctions = &vulkanFunctions;

            vkCheck(vmaCreateAllocator(&allocatorInfo, &ctx.vmaAllocator));
        }

        // Creation of pipeline layout global
        {
            VkPushConstantRange pushConstantRange {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = 128, 
            };

            VkPipelineLayoutCreateInfo pipelineLayoutCI {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 0, // Zero Descriptor Sets ! Tout est Bindless.
                .pSetLayouts = nullptr,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushConstantRange,
            };

            vkCheck(vkCreatePipelineLayout(ctx.device, &pipelineLayoutCI, nullptr, &ctx.commonPipelineLayoutGraphics));
        }

        // Creation of commands pools
        {
            VkCommandPoolCreateInfo poolCI {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = ctx.queueFamilyIndex,
            };
            vkCheck(vkCreateCommandPool(ctx.device, &poolCI, nullptr, &ctx.transientCmdPool));
            vkCheck(vkCreateCommandPool(ctx.device, &poolCI, nullptr, &ctx.mainCmdPool));
        }

        {
            VkSemaphoreTypeCreateInfo timelineCI {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                .initialValue = 0,
            };
            VkSemaphoreCreateInfo semaphoreCI {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = &timelineCI,
            };
            vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &ctx.queueTimelineSem));
        }

        return true;
    }

    Swapchain createSwapchain(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height, Uint32 framesInFlight) {
        Swapchain res{sys_alloc};

        VkSurfaceCapabilitiesKHR surfaceCaps{0};
        vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));

        Uint32 imageCount = MAX(MAX(2, surfaceCaps.minImageCount), framesInFlight);
        if (surfaceCaps.maxImageCount) assert(imageCount <= surfaceCaps.maxImageCount);

        Uint32 surfaceFormatCount{0};
        vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &surfaceFormatCount, nullptr));
        Array<VkSurfaceFormatKHR> surfaceFormats(temp_alloc); 
        surfaceFormats.resize(surfaceFormatCount);
        vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &surfaceFormatCount, surfaceFormats.data()));

        VkSurfaceFormatKHR surfaceFormat = surfaceFormats[0];
        for (Ulen i = 0; i < surfaceFormats.length(); i++) {
            if (surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM && surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = surfaceFormats[i];
                break;
            }
        }

        res.imageFormat = surfaceFormat.format;

        Uint32 presentModeCount{0};
        vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, nullptr));
        Array<VkPresentModeKHR> presentModes(temp_alloc); 
        presentModes.resize(presentModeCount);
        vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, presentModes.data()));

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (Ulen i = 0; i < presentModes.length(); i++) {
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = presentModes[i];
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
            .imageExtent = { res.width, res.height },
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = surfaceCaps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
        };
        vkCheck(vkCreateSwapchainKHR(ctx.device, &swapchainCI, nullptr, &res.handle));

        vkCheck(vkGetSwapchainImagesKHR(ctx.device, res.handle, &imageCount, nullptr));
        res.images.resize(imageCount);
        vkCheck(vkGetSwapchainImagesKHR(ctx.device, res.handle, &imageCount, res.images.data()));

        res.imageViews.resize(imageCount);
        res.presentSemaphores.resize(imageCount);

        for (Ulen i = 0; i < res.images.length(); i++) {
            VkImageViewCreateInfo imageViewCI {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = res.images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surfaceFormat.format,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            };
            vkCheck(vkCreateImageView(ctx.device, &imageViewCI, nullptr, &res.imageViews[i]));

            TextureInfo texInfo = { .handle = res.images[i] };
            ImageViewInfo imageInfo {
                .info = imageViewCI,
                .view = res.imageViews[i]
            };
            texInfo.views.push_back(imageInfo);
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

        for (auto handle : swapchain.textureHandles) {
            TextureInfo* texInfo = ctx.textures.get(handle);
            texInfo->views.destroy();
            ctx.textures.remove(handle);
        }
        swapchain.textureHandles.destroy();

    }

    void swapchainInit(TemporaryAllocator& temp_alloc, VkSurfaceKHR surface, Uint32 width, Uint32 height, Uint32 framesInFlight) {
        ctx.framesInFlight = framesInFlight;
        ctx.surface = surface;

        VkSurfaceCapabilitiesKHR surfaceCaps{0};
        vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));
        auto extent = surfaceCaps.currentExtent;
        if (extent.width == UINT32_MAX || extent.height == UINT32_MAX) {
            extent.width = width;
            extent.height = height;
        }
        assert(extent.width != UINT32_MAX && extent.height != UINT32_MAX);

        ctx.swapchain = createSwapchain(temp_alloc, MAX(extent.width, 1), MAX(extent.height, 1), ctx.framesInFlight);
    }

    void swapchainResize(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height) {
        //queueWaitIdle(ctx.queue);
        vkQueueWaitIdle(ctx.queue);
        destroySwapchain(ctx.swapchain);

        VkSurfaceCapabilitiesKHR surfaceCaps;
        vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &surfaceCaps));
        auto extent = surfaceCaps.currentExtent;
        if (extent.width == UINT32_MAX || extent.height == UINT32_MAX) {
            extent.width = width;
            extent.height = height;
        }
        assert(extent.width != UINT32_MAX && extent.height != UINT32_MAX);

        ctx.swapchain = createSwapchain(temp_alloc, extent.width, extent.height, ctx.framesInFlight);
    }

    void swapchainInitFromGlfw(TemporaryAllocator& temp_alloc, GLFWwindow *window, Uint32 framesInFlight) {
        Sint32 width, height;
        glfwGetWindowSize(window, &width, &height);
        //ctx.framesInFlight = framesInFlight;

        swapchainInit(temp_alloc, ctx.surface, MAX(0, width), MAX(0, height), framesInFlight);
    }

    static ShaderHandle shaderCreateInternal(Slice<Uint8> shaderCode, Uint32 groupSizeX = 1, Uint32 groupSizeY = 1, Uint32 groupSizeZ = 1) {
        VkShaderModuleCreateInfo moduleCI = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shaderCode.length(),
            .pCode = (Uint32*)shaderCode.data(),
        };
        VkShaderModule vkModule{0};
        vkCheck(vkCreateShaderModule(ctx.device, &moduleCI, nullptr, &vkModule));

        ShaderInfo shader {
            .shaderModule = vkModule,
            .currentWorkgroupSize = {groupSizeX, groupSizeY, groupSizeZ},
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
        
    void shutdown() {
        destroySwapchain(ctx.swapchain);

        if (ctx.device) {
            vkDeviceWaitIdle(ctx.device);

            ctx.freeCommandBuffers.destroy();

            vkDestroySemaphore(ctx.device, ctx.queueTimelineSem, nullptr);
            vkDestroyCommandPool(ctx.device, ctx.mainCmdPool, nullptr);

            if (ctx.transientCmdPool) {
                vkDestroyCommandPool(ctx.device, ctx.transientCmdPool, nullptr);
            }

            if (ctx.commonPipelineLayoutGraphics) {
                vkDestroyPipelineLayout(ctx.device, ctx.commonPipelineLayoutGraphics, nullptr);
            }

            if (ctx.vmaAllocator) {
                vmaDestroyAllocator(ctx.vmaAllocator);
            }
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

        
    }

    void waitIdle() {
        vkDeviceWaitIdle(ctx.device);
    }

    PipelineHandle graphicsPipelineCreate(TemporaryAllocator& temp_alloc, PipelineDesc desc) {
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

        // Input Assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssembly {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = false,
            //.topology = toVkTopology(desc.topology),
            //.primitiveRestartEnable = false,
        };

        // 4. Viewport (Dynamique)
        VkPipelineViewportStateCreateInfo viewportState {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        // 5. Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            //.polygonMode = VK_POLYGON_MODE_FILL,
            //.cullMode = toVkCullMode(desc.cullMode),
            //.frontFace = toVkFrontFace(desc.frontFace),
            .lineWidth = 1.0,
        };

        // 6. Multisample (1 sample par defaut pour l'instant)
        VkPipelineMultisampleStateCreateInfo multisample {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        // 7. Depth Stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            //.depthTestEnable = (desc.depthState.mode & DepthFlags::Read),
            //.depthWriteEnable = (desc.depthState.mode & DepthFlags::Write),
            //.depthCompareOp = toVkCompareOp(desc.depthState.compare),
            // On pourrait ajouter stencil ici si besoin
        };

        // 8. Color Blend
        //Array<VkPipelineColorBlendAttachmentState> blendAttachments(temp_alloc);
        //blendAttachments.resize(colorFormats.length());
        //for (auto& blendAttachment : blendAttachments) {
        //    auto mask = desc.blendState.colorWriteMask;
        //    if (mask == 0) {
        //        mask = VK_COLOR_COMPONENT_R_BIT
        //            | VK_COLOR_COMPONENT_G_BIT
        //            | VK_COLOR_COMPONENT_B_BIT
        //            | VK_COLOR_COMPONENT_A_BIT;
        //    }

        //    blendAttachment = {
        //        .blendEnable = desc.blendState.enable,
        //        .srcColorBlendFactor = toVkBlendFactor(desc.blendState.srcColorFactor),
        //        .dstColorBlendFactor = toVkBlendFactor(desc.blendState.dstColorFactor),
        //        .colorBlendOp = toVkBlendOp(desc.blendState.colorOp),
        //        .srcAlphaBlendFactor = toVkBlendFactor(desc.blendState.srcAlphaFactor),
        //        .dstAlphaBlendFactor = toVkBlendFactor(desc.blendState.dstAlphaFactor),
        //        .alphaBlendOp = toVkBlendOp(desc.blendState.alphaOp),
        //        .colorWriteMask = mask,
        //    };
        //}

        //VkPipelineColorBlendStateCreateInfo colorBlend {
        //    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        //    .attachmentCount = (Uint32)blendAttachments.length(),
        //    .pAttachments = blendAttachments.data(),
        //};
        VkPipelineColorBlendStateCreateInfo colorBlend {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        };

        // 9. Dynamic States
        // On veut pouvoir changer la taille de la fenetre sans recreer le pipeline
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,

            // Core Vulkan 1.3 (provenant de EDS1 et EDS2)
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

        // 10. Vertex Input (Vide car on utilise Pull-Mode / Buffer Address)
        VkPipelineVertexInputStateCreateInfo vertexInput {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        // Creation
        VkGraphicsPipelineCreateInfo pipelineCI {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingInfo,
            .stageCount = (Uint32)stages.length(),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssembly, // ignore thanks to dynamic state
            .pViewportState = &viewportState,      // ignore
            .pRasterizationState = &rasterizer,    // partially ignore
            .pMultisampleState = &multisample,
            .pDepthStencilState = &depthStencil,   // ignored
            .pColorBlendState = &colorBlend,       // ignored
            .pDynamicState = &dynamicInfo,         // now
            .layout = ctx.commonPipelineLayoutGraphics,
        };

        VkPipeline pipeline{0};
        vkCheck(vkCreateGraphicsPipelines(ctx.device, 0, 1, &pipelineCI, nullptr, &pipeline));

        GraphicsPipelineInfo gpInfo {
            .handle = pipeline,
        };
        return ctx.graphicsPipelines.add(gpInfo).value();
    }

    // Memory
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
        case AllocationType::Descriptor:
            bufUsage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
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
            .bufferHandle = buffer,
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

    void memFreeRaw(RawPtr ptr) {
        auto allocInfo = ctx.allocs.get(ptr.handle);
        vmaDestroyBuffer(ctx.vmaAllocator, allocInfo->bufferHandle, allocInfo->allocation);
        ctx.allocs.remove(ptr.handle);
    }

    // Arena
    static constexpr Uint64 alignUp(Uint64 value, Uint64 alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void Arena::init(Uint64 blockSize, Memory memType) {
        blockSize_ = blockSize;
        memType_ = memType;
        currentOffset_ = 0;
        currentBlockIdx_ = 0;
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

    Arena::Block Arena::allocateBlock(Uint64 requiredSize) {
        RawPtr raw = memAllocRaw(requiredSize, 1, 16, memType_, AllocationType::Default);
        return Block { raw, requiredSize };
    }

    RawPtr Arena::allocRaw(Uint64 sizeType, Uint64 count, Uint64 alignment) {
        auto size = sizeType * count;
        if (blocks_.is_empty()) {
            // C'est la toute premiere allocation, on cree le premier gros bloc
            blocks_.push_back(allocateBlock(MAX(blockSize_, size)));
        }

        Uint64 alignedOffset = alignUp(currentOffset_, alignment);

        // Si l'allocation depasse l'espace restant dans le bloc courant
        if (alignedOffset + size > blocks_[currentBlockIdx_].size) {
            currentBlockIdx_++;
            currentOffset_ = 0;
            alignedOffset = 0; // Au debut du bloc, c'est deja  garanti aligne par memAllocRaw

            // Si on a Depuis tous les blocs recycles, on en cree un nouveau
            if (currentBlockIdx_ >= blocks_.length()) {
                blocks_.push_back(allocateBlock(MAX(blockSize_, size)));
            }
        }

        Block& block = blocks_[currentBlockIdx_];

        RawPtr ptr;
        if (block.ptr.cpu) {
            ptr.cpu = static_cast<Uint8*>(block.ptr.cpu) + alignedOffset;
        } else {
            ptr.cpu = nullptr; // Cas ou Memory::GPU est utilise (non mappable sur CPU)
        }
        ptr.gpu = block.ptr.gpu + alignedOffset;
        
        // On associe le pointeur au Handle Vulkan racine (Utile en interne pour des assertions)
        ptr.handle = block.ptr.handle; 

        currentOffset_ = alignedOffset + size;

        return ptr;
    }

    void graphicsPipelineDestroy(PipelineHandle pipeline) {
        auto info = ctx.graphicsPipelines.get(pipeline);
        vkDestroyPipeline(ctx.device, info->handle, nullptr);
        ctx.graphicsPipelines.remove(pipeline);
    }

    CommandBuffer commandsBegin(Queue queue) {
        // 1. Chercher si on a un buffer libre et terminé sur le GPU
        if (FreeCommandBuffer* freeCb = ctx.freeCommandBuffers.peek()) {
            
            Uint64 currentGpuValue = 0;
            vkGetSemaphoreCounterValue(ctx.device, ctx.queueTimelineSem, &currentGpuValue);

            // Le GPU a dépassé la valeur de la timeline, le buffer est donc disponible !
            if (currentGpuValue >= freeCb->timelineValue) {
                // On extrait de la file (pop)
                gpu::CommandBuffer handleToReuse = ctx.freeCommandBuffers.pop()->handle;

                CommandBufferInfo* info = ctx.commandBuffers.get(handleToReuse);
                
                VkCommandBufferBeginInfo beginInfo {
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkCheck(vkBeginCommandBuffer(info->handle, &beginInfo));
                
                info->recording = true;
                info->numWaitSems = 0;
                info->numSignalSems = 0;
                
                return handleToReuse;
            }
        }

        // 2. Si aucun buffer n'est libre (ou qu'ils sont tous encore en cours de lecture par le GPU), on en crée un nouveau.
        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx.mainCmdPool, // On utilise le pool principal !
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer vkCmd;
        vkCheck(vkAllocateCommandBuffers(ctx.device, &allocInfo, &vkCmd));

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(vkCmd, &beginInfo));

        CommandBufferInfo info {
            .handle = vkCmd,
            .queue = queue,
            .recording = true,
        };

        return ctx.commandBuffers.add(info).value();
    }

    static Bool getBufferAndOffset(RawPtr ptr, VkBuffer& outBuffer, VkDeviceSize& outOffset) {
        if (!ptr.handle.is_valid()) return false;

        AllocInfo* info = ctx.allocs.get(ptr.handle);
        if (!info) return false;

        outBuffer = info->bufferHandle;
        outOffset = ptr.gpu - info->gpu;
        return true;
    }

    void cmdMemCpyRaw(CommandBuffer cmd, RawPtr dst, RawPtr src, Uint64 bytes) {
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

    void cmdBarrier(CommandBuffer cmd, Stage before, Stage after, Hazard hazards) {
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


    SemaphoreHandle semaphoreCreate(Uint64 initValue) {
        // L'architecture "No Graphics API" se repose entièrement sur les Timeline Semaphores (Vulkan 1.2+)
        VkSemaphoreTypeCreateInfo timelineCI {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = initValue,
        };

        VkSemaphoreCreateInfo semaphoreCI {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timelineCI, // <--- CRITIQUE
        };

        VkSemaphore vkSem = VK_NULL_HANDLE;
        vkCheck(vkCreateSemaphore(ctx.device, &semaphoreCI, nullptr, &vkSem));

        return ctx.semaphores.add(vkSem).value();
    }

    void semaphoreDestroy(SemaphoreHandle sem) {
        if (VkSemaphore* vkSem = ctx.semaphores.get(sem)) {
            vkDestroySemaphore(ctx.device, *vkSem, nullptr);
            ctx.semaphores.remove(sem);
        }
    }

    void semaphoreWait(SemaphoreHandle sem, Uint64 value) {
        if (VkSemaphore *vkSem = ctx.semaphores.get(sem)) {
            VkSemaphoreWaitInfo waitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = vkSem,
                .pValues = &value,
            };

            vkCheck(vkWaitSemaphores(ctx.device, &waitInfo, UINT64_MAX));
        }
    }

    void cmdAddWaitSemaphore(CommandBuffer cmd, SemaphoreHandle sem, Uint64 value) {
        if (auto cbInfo = ctx.commandBuffers.get(cmd)) {
            assert(cbInfo->numWaitSems < 4);
            cbInfo->waitSems[cbInfo->numWaitSems++] = { sem, value };
        }
    }

    void cmdAddSignalSemaphore(CommandBuffer cmd, SemaphoreHandle sem, Uint64 value) {
        if (auto cbInfo = ctx.commandBuffers.get(cmd)) {
            assert(cbInfo->numSignalSems < 4);
            cbInfo->signalSems[cbInfo->numSignalSems++] = { sem, value };
        }
    }

    void queueSubmit(Queue queue, CommandBuffer cmd) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        if (cbInfo->recording) {
            vkCheck(vkEndCommandBuffer(cbInfo->handle));
            cbInfo->recording = false;
        }

        // Incrémentation de la timeline interne de la Queue
        ctx.queueTimelineValue++;
        Uint64 submitTimelineValue = ctx.queueTimelineValue;

        // Préparation des sémaphores utilisateurs + notre sémaphore interne
        VkSemaphore vkWaitSems[5]; // +1 pour sécurité
        Uint64 waitValues[5];
        VkPipelineStageFlags waitStages[5];
        
        for (Uint32 i = 0; i < cbInfo->numWaitSems; i++) {
            vkWaitSems[i] = *ctx.semaphores.get(cbInfo->waitSems[i].sem);
            waitValues[i] = cbInfo->waitSems[i].value;
            waitStages[i] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        VkSemaphore vkSignalSems[5];
        Uint64 signalValues[5];

        for (Uint32 i = 0; i < cbInfo->numSignalSems; ++i) {
            vkSignalSems[i] = *ctx.semaphores.get(cbInfo->signalSems[i].sem);
            signalValues[i] = cbInfo->signalSems[i].value;
        }

        // AJOUT : On signale notre sémaphore interne pour savoir quand ce buffer a fini
        vkSignalSems[cbInfo->numSignalSems] = ctx.queueTimelineSem;
        signalValues[cbInfo->numSignalSems] = submitTimelineValue;

        VkTimelineSemaphoreSubmitInfo timelineInfo {
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .waitSemaphoreValueCount = cbInfo->numWaitSems,
            .pWaitSemaphoreValues = waitValues,
            .signalSemaphoreValueCount = cbInfo->numSignalSems + 1, // On ajoute le nôtre
            .pSignalSemaphoreValues = signalValues,
        };

        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timelineInfo,
            .waitSemaphoreCount = cbInfo->numWaitSems,
            .pWaitSemaphores = vkWaitSems,
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cbInfo->handle,
            .signalSemaphoreCount = cbInfo->numSignalSems + 1, // On ajoute le nôtre
            .pSignalSemaphores = vkSignalSems,
        };

        vkCheck(vkQueueSubmit(ctx.queue, 1, &submitInfo, VK_NULL_HANDLE));

        // CRITIQUE : On ajoute ce buffer à la file de recyclage !
        // Il ne sera réutilisé par commandsBegin() que lorsque ctx.queueTimelineSem >= submitTimelineValue
        ctx.freeCommandBuffers.push({ cmd, submitTimelineValue });
    }

    void cmdBeginRenderPass(CommandBuffer cmd, const RenderPassDesc& desc) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        assert(desc.colorAttachments.length() > 0);
        auto& colAtt = desc.colorAttachments[0];

        VkRenderingAttachmentInfo colorAttachment {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = colAtt.texture.view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = colAtt.loadOp == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = colAtt.storeOp == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {
                .color = { { colAtt.clearColor[0], colAtt.clearColor[1], colAtt.clearColor[2], colAtt.clearColor[3] } },
            }
        };

        VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                {0, 0},
                {desc.renderArea[0], desc.renderArea[1]},
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };

        vkCmdBeginRendering(cbInfo->handle, &renderingInfo);

        VkViewport viewport {
            0.0f, 0.0f,
            (Float32)desc.renderArea[0],
            (Float32)desc.renderArea[1],
            0.0f,
            1.0f
        };
        vkCmdSetViewport(cbInfo->handle, 0, 1, &viewport);

        VkRect2D scissor {
            {0, 0},
            {desc.renderArea[0], desc.renderArea[1]},
        };

        vkCmdSetScissor(cbInfo->handle, 0, 1, &scissor);
    }

    void cmdEndRenderPass(CommandBuffer cmd) {
        if (auto cbInfo = ctx.commandBuffers.get(cmd)) {
            vkCmdEndRendering(cbInfo->handle);
        }
    }

    void cmdBindGraphicsPipeline(CommandBuffer cmd, PipelineHandle pipeline) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        auto pInfo = ctx.graphicsPipelines.get(pipeline);
        if (cbInfo && pInfo) {
            vkCmdBindPipeline(cbInfo->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pInfo->handle);
        }
    }

    struct GraphicsPushConstants {
        Uint64 vertexData;
        Uint64 fragmentData;
    };

    void cmdDrawIndexedInstanced(CommandBuffer cmd, RawPtr vertexData, RawPtr fragmentData, RawPtr indices, Uint32 indexCount, Uint32 instanceCount) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        GraphicsPushConstants pc {
            .vertexData = vertexData.gpu,
            .fragmentData = fragmentData.gpu,
        };
        vkCmdPushConstants(cbInfo->handle, ctx.commonPipelineLayoutGraphics, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GraphicsPushConstants), &pc);

        VkBuffer idxBuffer;
        VkDeviceSize idxOffset;
        if (getBufferAndOffset(indices, idxBuffer, idxOffset)) {
            vkCmdBindIndexBuffer(cbInfo->handle, idxBuffer, idxOffset, VK_INDEX_TYPE_UINT32);
        }

        vkCmdDrawIndexed(cbInfo->handle, indexCount, instanceCount, 0, 0, 0);
    }

    Texture swapchainAcquireNext() {
        VkFenceCreateInfo fenceCI {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        VkFence fence;
        vkCheck(vkCreateFence(ctx.device, &fenceCI, nullptr, &fence));

        vkCheck(vkAcquireNextImageKHR(ctx.device, ctx.swapchain.handle, UINT64_MAX, VK_NULL_HANDLE, fence, &ctx.swapchain.currentImageIndex));

        vkCheck(vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, UINT64_MAX));
        vkDestroyFence(ctx.device, fence, nullptr);

        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx.transientCmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer cmd;
        vkCheck(vkAllocateCommandBuffers(ctx.device, &allocInfo, &cmd));

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(cmd, &beginInfo));

        VkImageMemoryBarrier2 transition {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = ctx.swapchain.images[ctx.swapchain.currentImageIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &transition
        };
        vkCmdPipelineBarrier2(cmd, &depInfo);
        vkCheck(vkEndCommandBuffer(cmd));

        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd
        };
        vkCheck(vkQueueSubmit(ctx.queue, 1, &submitInfo, VK_NULL_HANDLE));
        vkQueueWaitIdle(ctx.queue); 

        vkFreeCommandBuffers(ctx.device, ctx.transientCmdPool, 1, &cmd);

        return Texture {
            .width = ctx.swapchain.width,
            .height = ctx.swapchain.height,
            .format = ctx.swapchain.imageFormat,
            .image = ctx.swapchain.images[ctx.swapchain.currentImageIndex],
            .view = ctx.swapchain.imageViews[ctx.swapchain.currentImageIndex]
        };
    }

    void swapchainPresent(Queue queue, SemaphoreHandle waitSem, Uint64 waitValue) {
        Uint32 imageIdx = ctx.swapchain.currentImageIndex;
        VkSemaphore presentSemaphore = ctx.swapchain.presentSemaphores[imageIdx];

        // 1. Cmd Buffer pour passer de GENERAL -> PRESENT_SRC_KHR
        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx.transientCmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkCommandBuffer cmd;
        vkCheck(vkAllocateCommandBuffers(ctx.device, &allocInfo, &cmd));

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        vkCheck(vkBeginCommandBuffer(cmd, &beginInfo));

        VkImageMemoryBarrier2 transition {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = ctx.swapchain.images[imageIdx],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &transition
        };
        vkCmdPipelineBarrier2(cmd, &depInfo);
        vkCheck(vkEndCommandBuffer(cmd));

        // 2. Submit avec Timeline Semaphore
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore vkWaitSem = *ctx.semaphores.get(waitSem);
        
        VkTimelineSemaphoreSubmitInfo timelineInfo {
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .waitSemaphoreValueCount = 1,
            .pWaitSemaphoreValues = &waitValue,
        };

        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timelineInfo,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &vkWaitSem,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &presentSemaphore
        };

        vkCheck(vkQueueSubmit(ctx.queue, 1, &submitInfo, VK_NULL_HANDLE));

        // 3. Presentation
        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &ctx.swapchain.handle,
            .pImageIndices = &imageIdx
        };

        VkResult res = vkQueuePresentKHR(ctx.queue, &presentInfo);
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
            vkCheck(res);
        }

        vkQueueWaitIdle(ctx.queue);

        vkFreeCommandBuffers(ctx.device, ctx.transientCmdPool, 1, &cmd);
    }

    void cmdSetBlendState(CommandBuffer cmd, const BlendState& state) {
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

    void cmdSetDepthState(CommandBuffer cmd, const DepthState& state) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        vkCmdSetDepthTestEnable(cbInfo->handle, (state.mode & DepthFlags::Read) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(cbInfo->handle, (state.mode & DepthFlags::Write) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(cbInfo->handle, toVkCompareOp(state.compare));
    }

    void cmdSetRasterizerState(CommandBuffer cmd, CullMode cull, FrontFace face, PrimitiveTopology topology) {
        auto cbInfo = ctx.commandBuffers.get(cmd);
        if (!cbInfo) return;

        vkCmdSetCullMode(cbInfo->handle, toVkCullMode(cull));
        vkCmdSetFrontFace(cbInfo->handle, toVkFrontFace(face));
        vkCmdSetPrimitiveTopology(cbInfo->handle, toVkTopology(topology));
    }
}
