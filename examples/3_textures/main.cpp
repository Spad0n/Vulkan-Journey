#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "glm/glm.hpp"
#include "gpu.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace ctl;

constexpr Uint32 SCREEN_WIDTH     = 800;
constexpr Uint32 SCREEN_HEIGHT    = 800;
constexpr Uint32 FRAMES_IN_FLIGHT = 3;

struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

gpu::TextureHandle loadTexture(gpu::CommandBufferHandle cmd, gpu::Arena& uploadArena, const char* path) {
    int w, h, channels;
    stbi_uc* pixels = stbi_load(path, &w, &h, &channels, 4);
    assert(pixels && "Echec du chargement de l'image");

    Uint64 size = w * h * 4;
    gpu::RawPtr staging = uploadArena.allocRaw(size, 1, 16);
    memcpy(staging.cpu, pixels, size);
    stbi_image_free(pixels);

    gpu::TextureHandle tex = gpu::textureCreate(w, h, TextureFormat::RGBA8_Unorm);
    gpu::cmdCopyToTexture(cmd, tex, staging);
    return tex;
}

int main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc{sys_alloc};

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Bindless Textures", nullptr, nullptr);
    defer(glfwDestroyWindow(window));

    if (!gpu::init(temp_alloc, window)) return 1;
    defer(gpu::shutdown());

    gpu::swapchainInit(temp_alloc, SCREEN_WIDTH, SCREEN_HEIGHT, FRAMES_IN_FLIGHT);

    gpu::ShaderHandle vs = gpu::shaderCreate(temp_alloc, "shaders/textures.vert.spv");
    gpu::ShaderHandle fs = gpu::shaderCreate(temp_alloc, "shaders/textures.frag.spv");
    defer({ gpu::shaderDestroy(vs); gpu::shaderDestroy(fs); });

    TextureFormat formats[] = { TextureFormat::BGRA8_SRGB }; // Swapchain format
    gpu::PipelineDesc pDesc { vs, fs, formats };
    gpu::GraphicsPipelineHandle pipeline = gpu::graphicsPipelineCreate(temp_alloc, pDesc);
    defer(gpu::graphicsPipelineDestroy(pipeline));

    auto textureHeap = gpu::memAllocRaw(gpu::getTextureDescriptorSize(), 65536, 64, Memory::Default, AllocationType::TextureDescriptor);
    auto samplerHeap = gpu::memAllocRaw(gpu::getSamplerDescriptorSize(), 10, 64, Memory::Default, AllocationType::SamplerDescriptor);
    defer({ gpu::memFreeRaw(textureHeap); gpu::memFreeRaw(samplerHeap); });

    auto initArena = gpu::Arena(sys_alloc);
    defer(initArena.destroy());

    gpu::SlicePtr<Vertex> verts = initArena.alloc<Vertex>(4);
    verts[0] = { {-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f} };
    verts[1] = { { 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f} };
    verts[2] = { { 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f} };
    verts[3] = { {-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f} };

    gpu::SlicePtr<Uint32> indices = initArena.alloc<Uint32>(6);
    indices[0] = 0; indices[1] = 2; indices[2] = 1;
    indices[3] = 0; indices[4] = 1; indices[5] = 3;

    auto vertsLocal = gpu::memAlloc<Vertex>(4, Memory::GPU);
    auto indicesLocal = gpu::memAlloc<Uint32>(6, Memory::GPU);
    defer({ gpu::memFree(vertsLocal); gpu::memFree(indicesLocal); });

    auto uploadCmd = gpu::commandsBegin();
    gpu::cmdMemCpy(uploadCmd, vertsLocal, verts, 4 * sizeof(Vertex));
    gpu::cmdMemCpy(uploadCmd, indicesLocal, indices, 6 * sizeof(Uint32));

    gpu::TextureHandle peachTex = loadTexture(uploadCmd, initArena, "textures/peach.png");
    gpu::TextureHandle bowserTex = loadTexture(uploadCmd, initArena, "textures/bowser.png");
    gpu::SamplerHandle sampler = gpu::samplerCreate();
    defer({
        gpu::textureDestroy(peachTex);
        gpu::textureDestroy(bowserTex);
        gpu::samplerDestroy(sampler);
    });

    gpu::cmdBarrier(uploadCmd, Stage::Transfer, Stage::All, Hazard::None);
    gpu::queueSubmit(uploadCmd);
    gpu::waitIdle();

    // 4. Ecriture dans les Heaps
    gpu::writeTextureDescriptor(textureHeap, 0, peachTex);
    gpu::writeTextureDescriptor(textureHeap, 1, bowserTex);
    gpu::writeSamplerDescriptor(samplerHeap, 0, sampler);

    Float64 time = 0.0;

    // --- Boucle Principale ---
    while (!glfwWindowShouldClose(window)) {
        temp_alloc.reset();
        glfwPollEvents();

        Sint32 width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) { glfwWaitEvents(); continue; }

        Uint32 actualWidth, actualHeight;
        if (!gpu::acquireNextImage(actualWidth, actualHeight)) {
            gpu::recreateSwapchain(temp_alloc, width, height);
            continue;
        }

        time += 0.016;
        Float32 fade = (sin(time * 1.7) * 0.5f) + 0.5f;

        auto cmdBuf = gpu::commandsBegin();
        gpu::Arena& frameArena = gpu::getFrameArena();

        gpu::cmdBeginRendering(cmdBuf, LoadOp::Clear, StoreOp::Store, 0.2f, 0.2f, 0.2f, 1.0f);
        gpu::cmdSetViewportScissor(cmdBuf, actualWidth, actualHeight);
        
        gpu::cmdBindGraphicsPipeline(cmdBuf, pipeline);
        
        gpu::cmdBindDescriptorHeaps(cmdBuf, textureHeap, samplerHeap);

        gpu::BlendState blend{};
        blend.enable = false;
        blend.colorWriteMask = 0xF;
        gpu::cmdSetBlendState(cmdBuf, blend);

        gpu::DepthState depth{};
        depth.mode = DepthFlags::None;
        gpu::cmdSetDepthState(cmdBuf, depth);

        gpu::cmdSetRasterizerState(cmdBuf, CullMode::None, FrontFace::CounterClockwise, PrimitiveTopology::TriangleList);

        struct VertData {
            Uint64 address;
        };
        auto vertData = frameArena.alloc<VertData>();
        vertData->address = vertsLocal.gpu;

        // Paramètres pour Slang
        struct FragData {
            Uint32 texture_a;
            Uint32 texture_b;
            Uint32 samplerIdx;
            Float32 fade;
        };
        auto fragData = frameArena.alloc<FragData>();
        fragData->texture_a = 0; // Index dans le textureHeap !
        fragData->texture_b = 1;
        fragData->samplerIdx = 0;
        fragData->fade = fade;

        gpu::cmdDrawIndexedInstanced(cmdBuf, vertData, fragData, indicesLocal, 6, 1);
        
        gpu::cmdEndRendering(cmdBuf);
        gpu::queueSubmit(cmdBuf);
        gpu::present();
    }

    gpu::waitIdle();
    return 0;
}
