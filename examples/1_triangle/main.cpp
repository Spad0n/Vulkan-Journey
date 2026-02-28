#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "glm/glm.hpp"
#include "gpu.hpp"

using namespace ctl;

constexpr Uint32 SCREEN_WIDTH     = 800;
constexpr Uint32 SCREEN_HEIGHT    = 800;
constexpr Uint32 FRAMES_IN_FLIGHT = 2;

static void errorCallback(Sint32 error, const char *description) {
    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
}

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

Sint32 main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc{sys_alloc};

    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    if (!glfwVulkanSupported()) return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "No graphics API", nullptr, nullptr);
    if (!window) return 1;
    defer(glfwDestroyWindow(window));

    if (!gpu::init(temp_alloc, window)) {
        fprintf(stderr, "Failed to init Vulkan\n");
        return 1;
    }
    defer(gpu::shutdown());

    gpu::swapchainInit(temp_alloc, SCREEN_WIDTH, SCREEN_HEIGHT, FRAMES_IN_FLIGHT);

    gpu::ShaderHandle vs = gpu::shaderCreate(temp_alloc, "shaders/shader.vert.spv");
    gpu::ShaderHandle fs = gpu::shaderCreate(temp_alloc, "shaders/shader.frag.spv");
    defer({
        gpu::shaderDestroy(vs);
        gpu::shaderDestroy(fs);
    });

    TextureFormat formats[] = { TextureFormat::BGRA8_SRGB };
    gpu::PipelineDesc pDesc {
        .vs = vs,
        .fs = fs,
        .colorFormats = formats,
        .depthFormat = TextureFormat::Default,
        .stencilFormat = TextureFormat::Default
    };
    gpu::GraphicsPipelineHandle pipeline = gpu::graphicsPipelineCreate(temp_alloc, pDesc);
    defer(gpu::graphicsPipelineDestroy(pipeline));

    auto initArena = gpu::Arena(sys_alloc);
    defer(initArena.destroy());

    gpu::SlicePtr<Vertex> verts = initArena.alloc<Vertex>(3);
    verts[0].position = { 0.0f, -0.5f, 0.0f }; verts[0].color = { 1.0f, 0.0f, 0.0f }; // Bas milieu, Rouge
    verts[1].position = { 0.5f,  0.5f, 0.0f }; verts[1].color = { 0.0f, 1.0f, 0.0f }; // Haut droite, Vert
    verts[2].position = {-0.5f,  0.5f, 0.0f }; verts[2].color = { 0.0f, 0.0f, 1.0f }; // Haut gauche, Bleu

    auto vertsLocal = gpu::memAlloc<Vertex>(3, Memory::GPU);
    defer(gpu::memFree(vertsLocal));

    auto uploadCmdBuffer = gpu::commandsBegin();
    gpu::cmdMemCpy<Vertex>(uploadCmdBuffer, vertsLocal, verts, verts.length());
    gpu::cmdBarrier(uploadCmdBuffer, Stage::Transfer, Stage::All);
    gpu::queueSubmit(uploadCmdBuffer);

    gpu::waitIdle();
        
    while (!glfwWindowShouldClose(window)) {
        temp_alloc.reset();
        glfwPollEvents();

        Sint32 width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) {
            glfwWaitEvents();
            continue;
        }

        Uint32 actualWidth, actualHeight;
        if (!gpu::acquireNextImage(actualWidth, actualHeight)) {
            gpu::recreateSwapchain(temp_alloc, width, height);
            continue;
        }

        gpu::CommandBufferHandle cmdBuf = gpu::commandsBegin();

        gpu::Arena& frameArena = gpu::getFrameArena();

        Float64 time = glfwGetTime();

        Float32 r = static_cast<Float32>((sin(time) + 1.0) * 0.25);
        Float32 g = static_cast<Float32>((sin(time + 2.0944) + 1.0) * 0.25);
        Float32 b = static_cast<Float32>((sin(time) + 4.18879) * 0.25);

        gpu::cmdBeginRendering(cmdBuf, LoadOp::Clear, StoreOp::Store, r, g, b, 1.0f);

        gpu::cmdSetViewportScissor(cmdBuf, SCREEN_WIDTH, SCREEN_HEIGHT);

        gpu::cmdBindGraphicsPipeline(cmdBuf, pipeline);

        gpu::BlendState blend{};
        blend.enable = false; // deactivate for now
        blend.colorWriteMask = 0xF; // RGBA
        gpu::cmdSetBlendState(cmdBuf, blend);

        gpu::DepthState depth{};
        depth.mode = DepthFlags::None;
        gpu::cmdSetDepthState(cmdBuf, depth);

        gpu::cmdSetRasterizerState(cmdBuf, CullMode::None, FrontFace::CounterClockwise, PrimitiveTopology::TriangleList);

        struct VertData {
            Uint64 vertsAddress;
        };
        auto vertsData = frameArena.alloc<VertData>();
        vertsData->vertsAddress = vertsLocal.gpu;

        gpu::cmdDrawIndexedInstanced(cmdBuf, vertsData, gpu::RawPtr{}, gpu::RawPtr{}, 3, 1);
        gpu::cmdEndRendering(cmdBuf);

        gpu::queueSubmit(cmdBuf);

        gpu::present();
    }

    gpu::waitIdle();

    return 0;
}
