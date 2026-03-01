#include <GLFW/glfw3.h>
#include <stdio.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "gpu.hpp"

using namespace ctl;

constexpr Uint32 NUM_ELEMENTS = 1024;

static void errorCallback(Sint32 error, const char *description) {
    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
}

struct ComputePushConstants {
    Uint64 dataAddress;
};

Sint32 main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc{sys_alloc};

    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    if (!glfwVulkanSupported()) return 1;

    // We create invisible screen juste for enable pipeline layout compute
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); 
    GLFWwindow *window = glfwCreateWindow(800, 800, "Compute Only", nullptr, nullptr);
    if (!window) return 1;
    defer(glfwDestroyWindow(window));

    if (!gpu::init(temp_alloc, window)) {
        fprintf(stderr, "Failed to init Vulkan\n");
        return 1;
    }
    defer(gpu::shutdown());

    gpu::swapchainInit(temp_alloc, 800, 800, 1);

    gpu::ShaderHandle csAdd = gpu::shaderCreate(temp_alloc, "shaders/add.spv");
    gpu::ShaderHandle csMul = gpu::shaderCreate(temp_alloc, "shaders/mul.spv");
    
    gpu::ComputePipelineHandle pipeAdd = gpu::computePipelineCreate(temp_alloc, csAdd);
    gpu::ComputePipelineHandle pipeMul = gpu::computePipelineCreate(temp_alloc, csMul);

    defer({
        gpu::computePipelineDestroy(pipeAdd);
        gpu::computePipelineDestroy(pipeMul);
        gpu::shaderDestroy(csAdd);
        gpu::shaderDestroy(csMul);
    });

    // memory CPU for sending data
    auto stagingBuffer = gpu::memAlloc<Float32>(NUM_ELEMENTS, Memory::Default);
    // VRAM buffer
    auto gpuBuffer = gpu::memAlloc<Float32>(NUM_ELEMENTS, Memory::GPU);
    // Readback buffer for reading from CPU
    auto readbackBuffer = gpu::memAlloc<Float32>(NUM_ELEMENTS, Memory::Readback);
    
    defer({
        gpu::memFree(stagingBuffer);
        gpu::memFree(gpuBuffer);
        gpu::memFree(readbackBuffer);
    });

    for (auto& stagingData : stagingBuffer.cpu) {
        stagingData = 1.0f;
    }

    auto cmd = gpu::commandsBegin();

    gpu::cmdMemCpyRaw(cmd, gpuBuffer, stagingBuffer, NUM_ELEMENTS * sizeof(Float32));

    gpu::cmdBarrier(cmd, Stage::Transfer, Stage::Compute, Hazard::None);

    ComputePushConstants pc { .dataAddress = gpuBuffer.gpu };

    gpu::cmdBindComputePipeline(cmd, pipeAdd);
    gpu::cmdPushConstantsCompute(cmd, &pc, sizeof(pc));
    gpu::cmdDispatch(cmd, NUM_ELEMENTS / 64, 1, 1); // 16 groupes de 64 threads = 1024

    gpu::cmdBarrier(cmd, Stage::Compute, Stage::Compute, Hazard::None);

    gpu::cmdBindComputePipeline(cmd, pipeMul);
    gpu::cmdPushConstantsCompute(cmd, &pc, sizeof(pc));
    gpu::cmdDispatch(cmd, NUM_ELEMENTS / 64, 1, 1);

    gpu::cmdBarrier(cmd, Stage::Compute, Stage::Transfer, Hazard::None);

    gpu::cmdMemCpy<Float32>(cmd, readbackBuffer, gpuBuffer, NUM_ELEMENTS);

    gpu::queueSubmit(cmd);

    gpu::waitIdle();

    Slice<Float32> result = readbackBuffer.cpu;
    
    printf("--- RESULTATS DU COMPUTE SHADER ---\n");
    printf("Formule  : (x + 10.0) * 2.0\n");
    printf("Valeur initiale : 1.0f\n");
    printf("Resultat attendu: 22.0f\n");
    printf("-----------------------------------\n");
    
    printf("Resultat [0]    : %f\n", result[0]);
    printf("Resultat [512]  : %f\n", result[512]);
    printf("Resultat [1023] : %f\n", result[1023]);

    return 0;
}
