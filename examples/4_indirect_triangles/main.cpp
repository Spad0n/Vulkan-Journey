#include <GLFW/glfw3.h>
#include <stdio.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "glm/glm.hpp"
#include "gpu.hpp"

#if defined(_WIN32) || defined(_WIN64)
#define _USE_MATH_DEFINES
#endif

#include <math.h>

using namespace ctl;

constexpr Uint32 SCREEN_WIDTH     = 800;
constexpr Uint32 SCREEN_HEIGHT    = 600;
constexpr Uint32 FRAMES_IN_FLIGHT = 3;
constexpr Uint32 NUM_TRIANGLES    = 32;

struct Vertex {
    glm::vec3 position;
};

struct IndirectData {
    gpu::DrawIndexedIndirectCommand cmd;
    glm::vec3 color;
    glm::vec3 pos;
    Float32 size;
};

// Helper for convert HSL to RGB
glm::vec3 hslToRgb(float h, float s, float l) {
    auto f = [&](float n) {
        float k = fmodf(n + h * 12.0f, 12.0f);
        float a = s * fminf(l, 1.0f - l);
        return l - a * fmaxf(-1.0f, fminf(fminf(k - 3.0f, 9.0f - k), 1.0f));
    };
    return { f(0), f(8), f(4) };
}

int main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc{sys_alloc};

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Indirect Triangles C++", nullptr, nullptr);
    
    gpu::init(temp_alloc, window);
    defer(gpu::shutdown());

    gpu::swapchainInit(temp_alloc, SCREEN_WIDTH, SCREEN_HEIGHT, FRAMES_IN_FLIGHT);

    // 1. Shaders & Pipeline
    gpu::ShaderHandle vs = gpu::shaderCreate(temp_alloc, "shaders/indirect.vert.spv");
    gpu::ShaderHandle fs = gpu::shaderCreate(temp_alloc, "shaders/indirect.frag.spv");
    defer({
        gpu::shaderDestroy(vs);
        gpu::shaderDestroy(fs);
    });
    
    TextureFormat formats[] = { TextureFormat::BGRA8_SRGB };
    gpu::PipelineDesc pDesc { .vs = vs, .fs = fs, .colorFormats = formats };
    gpu::GraphicsPipelineHandle pipeline = gpu::graphicsPipelineCreate(temp_alloc, pDesc);
    defer(gpu::graphicsPipelineDestroy(pipeline));

    // 2. triangle as a model (only one triangle shared)
    auto initArena = gpu::Arena(sys_alloc);
    defer(initArena.destroy());
    gpu::SlicePtr<Vertex> verts = initArena.alloc<Vertex>(3);
    verts[0].position = { -0.5f,  0.5f, 0.0f };
    verts[1].position = {  0.0f, -0.5f, 0.0f };
    verts[2].position = {  0.5f,  0.5f, 0.0f };

    gpu::SlicePtr<Uint32> indices = initArena.alloc<Uint32>(3);
    indices[0] = 0; indices[1] = 2; indices[2] = 1;

    // 3. Préparation des données Indirectes
    gpu::SlicePtr<IndirectData> indirectData = initArena.alloc<IndirectData>(NUM_TRIANGLES);
    float radius = 0.6f;
    for (Uint32 i = 0; i < NUM_TRIANGLES; i++) {
        float angle = (float)i / (float)NUM_TRIANGLES * 2.0f * M_PI;
        
        // render command
        indirectData[i].cmd = {
            .indexCount    = 3,
            .instanceCount = 1,
            .firstIndex    = 0,
            .vertexOffset  = 0,
            .firstInstance = 0
        };

        // user data
        indirectData[i].pos   = { cosf(angle) * radius, sinf(angle) * radius, 0.0f };
        indirectData[i].color = hslToRgb((float)i / NUM_TRIANGLES, 1.0f, 0.5f);
        indirectData[i].size  = 0.1f;
    }

    // Count buffer
    gpu::Ptr<Uint32> drawCount = initArena.alloc<Uint32>();
    *drawCount = NUM_TRIANGLES;

    // Allocate GPU and Upload
    auto vertsLocal     = gpu::memAlloc<Vertex>(3, Memory::GPU);
    auto indicesLocal   = gpu::memAlloc<Uint32>(3, Memory::GPU);
    auto indirectLocal  = gpu::memAlloc<IndirectData>(NUM_TRIANGLES, Memory::GPU);
    auto countLocal     = gpu::memAlloc<Uint32>(1, Memory::GPU);
    defer({
        gpu::memFree(vertsLocal);
        gpu::memFree(indicesLocal);
        gpu::memFree(indirectLocal);
        gpu::memFree(countLocal);
    });

    auto uploadCmd = gpu::commandsBegin();
    gpu::cmdMemCpy(uploadCmd, vertsLocal, verts, 3 * sizeof(Vertex));
    gpu::cmdMemCpy(uploadCmd, indicesLocal, indices, 3 * sizeof(Uint32));
    gpu::cmdMemCpy(uploadCmd, indirectLocal, indirectData, NUM_TRIANGLES * sizeof(IndirectData));
    gpu::cmdMemCpy(uploadCmd, countLocal, drawCount, sizeof(Uint32));
    gpu::cmdBarrier(uploadCmd, Stage::Transfer, Stage::All);
    gpu::queueSubmit(uploadCmd);

    gpu::waitIdle();

    // Boucle de rendu
    while (!glfwWindowShouldClose(window)) {
        temp_alloc.reset();
        glfwPollEvents();

        Uint32 w, h;
        if (!gpu::acquireNextImage(w, h)) {
            gpu::recreateSwapchain(temp_alloc, SCREEN_WIDTH, SCREEN_HEIGHT);
            continue;
        }

        gpu::CommandBufferHandle cmd = gpu::commandsBegin();
        gpu::Arena& frameArena = gpu::getFrameArena();

        gpu::cmdBeginRendering(cmd, LoadOp::Clear, StoreOp::Store, 0.1f, 0.1f, 0.1f, 1.0f);
        gpu::cmdSetViewportScissor(cmd, w, h);
        gpu::cmdBindGraphicsPipeline(cmd, pipeline);

        gpu::BlendState blend{};
        blend.enable = false; // deactivate for now
        blend.colorWriteMask = 0xF; // RGBA
        gpu::cmdSetBlendState(cmd, blend);

        gpu::DepthState depth{};
        depth.mode = DepthFlags::None;
        gpu::cmdSetDepthState(cmd, depth);

        // config dynamic states
        gpu::cmdSetRasterizerState(cmd, CullMode::None, FrontFace::CounterClockwise, PrimitiveTopology::TriangleList);
        gpu::DepthState ds = { .mode = DepthFlags::None };
        gpu::cmdSetDepthState(cmd, ds);

        struct VertData {
            Uint64 vertsAddress;
        };
        auto vData = frameArena.alloc<VertData>();
        vData->vertsAddress = vertsLocal.gpu;

        // Multi-Indirect call
        gpu::cmdDrawIndexedInstancedIndirectMulti(
            cmd, 
            vData,            // vertexData pointer
            gpu::RawPtr{},    // fragmentData
            indicesLocal,     // index buffer
            indirectLocal,    // indirect arguments buffer
            sizeof(IndirectData), 
            countLocal        // draw count buffer
        );

        gpu::cmdEndRendering(cmd);
        gpu::queueSubmit(cmd);
        gpu::present();
    }

    gpu::waitIdle();

    return 0;
}
