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

#define vkCheck(result) check_location((result), __FILE__, __LINE__)
static inline void check_location(Bool result, const char *filePath, Uint32 line) {
    // TODO: create cases on different error
    if (result != VK_SUCCESS) {
        fprintf(stderr, "%s:%d:Vulkan failure: %d\n", filePath, line, result);
    }
}

struct Context {
    constexpr Context(Allocator& allocator)
        : allocator{allocator}
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
};

static void errorCallback(Sint32 error, const char* description) {
    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
}

static void createInstance(Context& ctx, TemporaryAllocator& temp_alloc);

static void destroyInstance(Context& ctx);

static void createDevice(Context& ctx, TemporaryAllocator& temp_alloc);

static void destroyDevice(Context& ctx);

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

    while (!glfwWindowShouldClose(ctx.window)) {
        temp_alloc.reset();
        glfwPollEvents();
    }

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

    VkDeviceCreateInfo deviceCI {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = (Uint32)STATIC_LEN(queueCreateInfos),
        .pQueueCreateInfos = queueCreateInfos,
    };

    vkCheck(vkCreateDevice(ctx.physicalDevice, &deviceCI, 0, &ctx.device));

    vkGetDeviceQueue(ctx.device, ctx.queueFamilyIndex, 0, &ctx.queue);
}

static void destroyDevice(Context& ctx) {
    vkDestroyDevice(ctx.device, nullptr);
}
