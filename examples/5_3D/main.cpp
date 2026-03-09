#include <GLFW/glfw3.h>
#include <stdio.h>
#include "gpu.hpp"
#include "shared.hpp"
#include "macro_utils.hpp"

using namespace ctl;

struct ShaderVertexData {
    Uint64 positions;          
    Uint64 normals;            
    glm::mat4 modelToWorld;
    glm::mat4 modelToWorldNormal;
    glm::mat4 worldToView;
    glm::mat4 viewToProj;
};
static_assert(sizeof(ShaderVertexData) == 8+8+64+64+64+64);

struct MeshGPU {
    gpu::SlicePtr<glm::vec4> pos;
    gpu::SlicePtr<glm::vec4> normals;
    gpu::SlicePtr<Uint32> indices;
    Uint32 indexCount;
};

int main() {
    printf("Right-click + WASD for first-person controls.\n");

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "3D model", nullptr, nullptr);

    SystemAllocator sysAlloc;
    TemporaryAllocator tempAlloc(sysAlloc);
    
    if (!gpu::init(tempAlloc, window)) return -1;
    defer(gpu::shutdown());

    gpu::swapchainInit(tempAlloc, 800, 600, 2);

    gpu::ShaderHandle vertShader = gpu::shaderCreate(tempAlloc, "shaders/3D.vert.spv");
    gpu::ShaderHandle fragShader = gpu::shaderCreate(tempAlloc, "shaders/3D.frag.spv");
    defer({
        gpu::shaderDestroy(vertShader);
        gpu::shaderDestroy(fragShader);
    });

    TextureFormat formats[] = { TextureFormat::BGRA8_SRGB };
    gpu::PipelineDesc pDesc {
        .vs = vertShader,
        .fs = fragShader,
        .colorFormats = formats,
        .depthFormat = TextureFormat::D32_Float,
    };
    gpu::GraphicsPipelineHandle pipeline = gpu::graphicsPipelineCreate(tempAlloc, pDesc);
    defer(gpu::graphicsPipelineDestroy(pipeline));

    gpu::TextureHandle depthTex = gpu::textureCreate(1000, 1000, TextureFormat::D32_Float, TextureUsage::DepthAttachment);
    defer(gpu::textureDestroy(depthTex));

    Scene scene(sysAlloc);
    if (!loadSceneGltf("assets/sponza.glb", sysAlloc, scene)) {
        printf("Failed to load Sponza\n");
        return -1;
    }
    defer({
        scene.meshes.destroy();
        scene.instances.destroy();
    });

    Array<MeshGPU> meshesGPU(sysAlloc);
    defer(meshesGPU.destroy());
    auto uploadCmd = gpu::commandsBegin();
    
    for (auto& meshCPU : scene.meshes) {
        MeshGPU m;
        m.pos = gpu::memAlloc<glm::vec4>(meshCPU.pos.length(), Memory::GPU);
        m.normals = gpu::memAlloc<glm::vec4>(meshCPU.normals.length(), Memory::GPU);
        m.indices = gpu::memAlloc<Uint32>(meshCPU.indices.length(), Memory::GPU);
        m.indexCount = (Uint32)meshCPU.indices.length();

        auto& arena = gpu::getFrameArena();
        auto stagePos = arena.alloc<glm::vec4>(meshCPU.pos.length());
        auto stageNorm = arena.alloc<glm::vec4>(meshCPU.normals.length());
        auto stageIdx = arena.alloc<Uint32>(meshCPU.indices.length());

        for(Ulen i=0; i<meshCPU.pos.length(); i++) stagePos[i] = meshCPU.pos[i];
        for(Ulen i=0; i<meshCPU.normals.length(); i++) stageNorm[i] = meshCPU.normals[i];
        for(Ulen i=0; i<meshCPU.indices.length(); i++) stageIdx[i] = meshCPU.indices[i];

        gpu::cmdMemCpy(uploadCmd, m.pos, stagePos, meshCPU.pos.length() * sizeof(glm::vec4));
        gpu::cmdMemCpy(uploadCmd, m.normals, stageNorm, meshCPU.normals.length() * sizeof(glm::vec4));
        gpu::cmdMemCpy(uploadCmd, m.indices, stageIdx, meshCPU.indices.length() * sizeof(Uint32));

        meshesGPU.push_back(m);
    }
    gpu::cmdBarrier(uploadCmd, Stage::Transfer, Stage::All, Hazard::None);
    gpu::queueSubmit(uploadCmd);
    gpu::waitIdle(); 

    Float32 deltaTime = 0.1f;
    Float32 lastFrame = (Float32)glfwGetTime();
    Sint32 currentWidth  = 1000;
    Sint32 currentHeight = 1000;

    while (!glfwWindowShouldClose(window)) {
        tempAlloc.reset();

        if (!handleWindowEvents(window)) break;

        Float32 currentFrame = (Float32)glfwGetTime();
        deltaTime = glm::min(0.1f, currentFrame - lastFrame);
        lastFrame = currentFrame;

        Sint32 w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w == 0 || h == 0) { glfwWaitEvents(); continue; }

        if (w != currentWidth || h != currentHeight) {
            gpu::recreateSwapchain(tempAlloc, w, h);
            
            gpu::textureDestroy(depthTex);
            depthTex = gpu::textureCreate(w, h, TextureFormat::D32_Float, TextureUsage::DepthAttachment);
            
            currentWidth = w;
            currentHeight = h;
        }

        Uint32 actualW, actualH;
        if (!gpu::acquireNextImage(actualW, actualH)) {
            gpu::recreateSwapchain(tempAlloc, w, h);
            
            gpu::textureDestroy(depthTex);
            depthTex = gpu::textureCreate(w, h, TextureFormat::D32_Float, TextureUsage::DepthAttachment);
            continue;
        }

        glm::mat4 view = firstPersonCameraView(deltaTime); 
        
        Float32 aspectRatio = (Float32)actualW / (Float32)actualH;
        glm::mat4 proj = matrix4PerspectiveF32(glm::radians(59.0f), aspectRatio, 0.1f, 1000.0f, false);

        auto cmdBuf = gpu::commandsBegin();
        auto& frameArena = gpu::getFrameArena();

        gpu::cmdBeginRendering(cmdBuf, LoadOp::Clear, StoreOp::Store, 0.7f, 0.7f, 0.7f, 1.0f, depthTex);
        gpu::cmdSetViewportScissor(cmdBuf, actualW, actualH);
        
        gpu::cmdBindGraphicsPipeline(cmdBuf, pipeline);

        gpu::BlendState blend{};
        blend.enable = false;
        blend.colorWriteMask = 0xF;
        gpu::cmdSetBlendState(cmdBuf, blend);

        gpu::DepthState dState = { DepthFlags::Read | DepthFlags::Write, CompareOp::Less };
        gpu::cmdSetDepthState(cmdBuf, dState);
        gpu::cmdSetRasterizerState(cmdBuf, CullMode::Back, FrontFace::CounterClockwise, PrimitiveTopology::TriangleList);

        for (const auto& instance : scene.instances) {
            const auto& mesh = meshesGPU[instance.meshIdx];

            auto shaderData = frameArena.alloc<ShaderVertexData>();
            shaderData->positions = mesh.pos.gpu;
            shaderData->normals = mesh.normals.gpu;
            shaderData->modelToWorld = instance.transform;
            shaderData->modelToWorldNormal = glm::transpose(glm::inverse(instance.transform));
            shaderData->worldToView = view;
            shaderData->viewToProj = proj;

            gpu::cmdDrawIndexedInstanced(cmdBuf, shaderData, gpu::RawPtr{}, mesh.indices, mesh.indexCount, 1);
        }

        gpu::cmdEndRendering(cmdBuf);
        gpu::queueSubmit(cmdBuf);
        gpu::present();
    }

    gpu::waitIdle();
    
    for(auto& m : meshesGPU) {
        gpu::memFree(m.pos);
        gpu::memFree(m.normals);
        gpu::memFree(m.indices);
    }
    return 0;
}
