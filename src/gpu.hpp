#ifndef GPU_HPP
#define GPU_HPP
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "ctl/allocator.hpp"
#include "ctl/types.hpp"
#include "ctl/string.hpp"

using namespace ctl;

namespace gpu {
    struct Pipeline {
        VkPipeline handle;
        VkPipelineLayout layout;
    };

    struct Ptr {
        void* cpu;
        Uint64 gpu;

        template<typename T>
        T* as() { return (T*)cpu; }
    };

    void init(TemporaryAllocator& allocator, GLFWwindow *window, Uint32 width, Uint32 height);

    void waitIdle();

    void shutdown();

    VkCommandBuffer beginFrame();
    void endFrame();

    Ptr malloc(Uint64 size, Uint64 alignment = 16);

    template<typename T>
    Ptr alloc(Uint64 count = 1) {
        return malloc(sizeof(T) * count, alignof(T));
    }

    Pipeline createGraphicsPipeline(TemporaryAllocator& temp_alloc, StringView vertPath, StringView fragPath);
    void destroyPipeline(Pipeline p);

    void setPipeline(VkCommandBuffer cmd, Pipeline pipeline);
    void pushAddress(VkCommandBuffer cmd, Uint64 gpuAddress);
    void draw(VkCommandBuffer cmd, Uint32 vertexCount, Uint32 instanceCount, Uint32 firstVertex, Uint32 firstInstance);
}

#endif // GPU_HPP
