#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "gpu.hpp"

using namespace ctl;

struct Matrix {
    Matrix(Allocator& allocator, int row, int col)
        : data_(allocator)
        , rows_{row}
        , cols_{col}
    {
        data_.resize(row * col);
        for (auto& data : data_) {
            data = 0;
        }
    }

    int& operator ()(int i, int j) {
        return data_[i * cols_ + j];
    }

    const int& operator ()(int i, int j) const {
        return data_[i * cols_ + j];
    }

    int* data() {
        return data_.data();
    }

private:
    Array<int> data_;
    int rows_;
    int cols_;
};

// initialize to this matrices
// 0 1 0 0 1 0 1 0
// 1 0 0 0 1 1 0 0
// 0 0 0 0 1 1 1 1
// 0 0 0 0 1 0 0 0
// 1 1 1 1 0 0 1 0
// 0 1 1 0 0 0 1 0
// 1 0 1 0 1 1 0 0
// 0 0 1 0 0 0 0 0
void demo(Matrix& m) {
    m(0, 1) = 1; m(0, 4) = 1; m(0, 6) = 1;
    m(1, 0) = 1; m(1, 4) = 1; m(1, 5) = 1;
    m(2, 4) = 1; m(2, 5) = 1; m(2, 6) = 1; m(2, 7) = 1;
    m(3, 4) = 1; m(4, 0) = 1; m(4, 1) = 1; m(4, 2) = 1;
    m(4, 3) = 1; m(4, 6) = 1; m(5, 1) = 1; m(5, 2) = 1;
    m(5, 6) = 1; m(6, 0) = 1; m(6, 2) = 1; m(6, 4) = 1;
    m(6, 5) = 1; m(7, 2) = 1;
}

struct MatmulPC {
    Uint64 A;
    Uint64 B;
    Uint64 C;
    Uint32 N;
};

struct TracePC {
    Uint64 A;
    Uint64 result;
    Uint32 N;
};

int main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc{sys_alloc};

    // Initialize Vulkan (without window)
    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *window = glfwCreateWindow(800, 800, "Compute Triangles", nullptr, nullptr);
    defer(glfwDestroyWindow(window));

    if (!gpu::init(temp_alloc, window)) return 1;
    defer(gpu::shutdown());
    gpu::swapchainInit(temp_alloc, 800, 800, 1); // need to use swapchain to enable some features

    constexpr int N = 8;
    Matrix a(temp_alloc, N, N);
    demo(a);
    printf("N = %d\n", N);

    // load shaders and pipelines
    gpu::ShaderHandle csMatmul = gpu::shaderCreate(temp_alloc, "shaders/matmul.spv");
    gpu::ShaderHandle csTrace  = gpu::shaderCreate(temp_alloc, "shaders/trace.spv");
    gpu::ComputePipelineHandle pipeMatmul = gpu::computePipelineCreate(temp_alloc, csMatmul);
    gpu::ComputePipelineHandle pipeTrace  = gpu::computePipelineCreate(temp_alloc, csTrace);

    defer({
        gpu::computePipelineDestroy(pipeMatmul);
        gpu::computePipelineDestroy(pipeTrace);
        gpu::shaderDestroy(csMatmul);
        gpu::shaderDestroy(csTrace);
    });

    // allocate some memory
    auto staging_A = gpu::memAlloc<int>(N * N, Memory::Default);
    memcpy(staging_A.cpu.data(), a.data(), N * N * sizeof(int));

    auto staging_Zero = gpu::memAlloc<int>(1, Memory::Default);
    // TODO maybe implement memset on the gpu
    staging_Zero[0] = 0;

    auto d_A  = gpu::memAlloc<int>(N * N, Memory::GPU);
    auto d_A2 = gpu::memAlloc<int>(N * N, Memory::GPU);
    auto d_A3 = gpu::memAlloc<int>(N * N, Memory::GPU);
    
    auto d_trace = gpu::memAlloc<int>(1, Memory::GPU);
    auto readback_trace = gpu::memAlloc<int>(1, Memory::Readback);

    defer({
        gpu::memFree(staging_A); gpu::memFree(staging_Zero);
        gpu::memFree(d_A); gpu::memFree(d_A2); gpu::memFree(d_A3);
        gpu::memFree(d_trace); gpu::memFree(readback_trace);
    });

    auto cmd = gpu::commandsBegin();

    // Upload A et Zero
    gpu::cmdMemCpy(cmd, d_A, staging_A, N * N * sizeof(int));
    gpu::cmdMemCpy(cmd, d_trace, staging_Zero, sizeof(int));
    gpu::cmdBarrier(cmd, Stage::Transfer, Stage::Compute, Hazard::None);

    // Launch of A^2 = A * A
    gpu::cmdBindComputePipeline(cmd, pipeMatmul);
    MatmulPC pc1 = { d_A.gpu, d_A.gpu, d_A2.gpu, N };
    gpu::cmdPushConstantsCompute(cmd, &pc1, sizeof(pc1));
    gpu::cmdDispatch(cmd, (N + 15) / 16, (N + 15) / 16, 1);
    
    gpu::cmdBarrier(cmd, Stage::Compute, Stage::Compute, Hazard::None);

    // Launch of A^3 = A^2 * A
    MatmulPC pc2 = { d_A2.gpu, d_A.gpu, d_A3.gpu, N };
    gpu::cmdPushConstantsCompute(cmd, &pc2, sizeof(pc2));
    gpu::cmdDispatch(cmd, (N + 15) / 16, (N + 15) / 16, 1);

    gpu::cmdBarrier(cmd, Stage::Compute, Stage::Compute, Hazard::None);

    // Launch of trace kernel
    gpu::cmdBindComputePipeline(cmd, pipeTrace);
    TracePC pc3 = { d_A3.gpu, d_trace.gpu, N };
    gpu::cmdPushConstantsCompute(cmd, &pc3, sizeof(pc3));
    gpu::cmdDispatch(cmd, (N + 255) / 256, 1, 1);

    gpu::cmdBarrier(cmd, Stage::Compute, Stage::Transfer, Hazard::None);

    // load result
    gpu::cmdMemCpy(cmd, readback_trace, d_trace, sizeof(int));

    // execute the synchronization
    gpu::queueSubmit(cmd);
    gpu::waitIdle(); // equivalent of hipDeviceSynchronize() global

    int trace = readback_trace[0];
    printf("Trace(A^3) = %d\n", trace);
    printf("Number of triangles = %d\n", trace / 6);

    return 0;
}
