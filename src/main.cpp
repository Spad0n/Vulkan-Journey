#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include "gpu/gpu.hpp"

using namespace ctl;

constexpr Uint32 SCREEN_WIDTH  = 800;
constexpr Uint32 SCREEN_HEIGHT = 600;
constexpr Uint32 FramesInFlight = 3;

static void errorCallback(Sint32 error, const char *description) {
    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
}

glm::vec4 changingColor(Float32 deltaTime) {
    static float t = 0.0f;
    t = fmodf(t + deltaTime * 1.7f, glm::pi<float>() * 2.0f);

    glm::vec4 colorA{ 0.2f, 0.2f, 0.2f, 1.0f };
    glm::vec4 colorB{ 0.4f, 0.4f, 0.4f, 1.0f };
    
    float factor = sinf(t) * 0.5f + 0.5f;
    return glm::mix(colorA, colorB, factor);
}

Sint32 main() {
    SystemAllocator sys_alloc;
    TemporaryAllocator temp_alloc{sys_alloc};

    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    if (!glfwVulkanSupported()) return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Triangle (No Graphics API)", nullptr, nullptr);
    if (!window) return 1;
    defer(glfwDestroyWindow(window));

    Sint32 windowSizeX = SCREEN_WIDTH;
    Sint32 windowSizeY = SCREEN_HEIGHT;

    if (!gpu::init(temp_alloc, window)) {
        fprintf(stderr, "Failed to init vulkan\n");
        return 1;
    }
    defer(gpu::shutdown());

    gpu::swapchainInitFromGlfw(temp_alloc, window, FramesInFlight);

    auto vertShader = gpu::shaderCreate(temp_alloc, "shaders/spv/test.vert.spv");
    auto fragShader = gpu::shaderCreate(temp_alloc, "shaders/spv/test.frag.spv");
    defer(gpu::shaderDestroy(vertShader));
    defer(gpu::shaderDestroy(fragShader));

    // LE NOUVEAU PARADIGME : 
    // Le Pipeline ne contient plus AUCUN état de rasterization ou de blend !
    // Juste les shaders et le format d'image cible.
    TextureFormat formats[] = { TextureFormat::BGRA8_Unorm };
    gpu::PipelineDesc pipelineDesc {
        .vs = vertShader,
        .fs = fragShader,
        .colorFormats = ctl::Slice<TextureFormat>{formats},
        .depthFormat = TextureFormat::Default,
        .stencilFormat = TextureFormat::Default
    };
    auto pipeline = gpu::graphicsPipelineCreate(temp_alloc, pipelineDesc);
    defer(gpu::graphicsPipelineDestroy(pipeline));

    struct Vertex {
        glm::vec4 pos;
        glm::vec4 col;
    };
    
    auto initArena = gpu::Arena(sys_alloc);
    defer(initArena.destroy());
    initArena.init();

    gpu::SlicePtr<Vertex> verts = initArena.alloc<Vertex>(3);
    verts[0].pos = { -0.5f,  0.5f, 0.0f, 1.0f }; verts[0].col = { 1.0f, 0.0f, 0.0f, 1.0f };
    verts[1].pos = {  0.0f, -0.5f, 0.0f, 1.0f }; verts[1].col = { 0.0f, 1.0f, 0.0f, 1.0f };
    verts[2].pos = {  0.5f,  0.5f, 0.0f, 1.0f }; verts[2].col = { 0.0f, 0.0f, 1.0f, 1.0f };
    
    gpu::SlicePtr<Uint32> indices = initArena.alloc<Uint32>(3);
    indices[0] = 0; indices[1] = 2; indices[2] = 1;

    auto vertsLocal = gpu::memAlloc<Vertex>(3, Memory::GPU);
    defer(gpu::memFree(vertsLocal));
    auto indicesLocal = gpu::memAlloc<Uint32>(3, Memory::GPU);
    defer(gpu::memFree(indicesLocal));

    auto uploadCmdBuffer = gpu::commandsBegin(Queue::Main);
    gpu::cmdMemCpy<Vertex>(uploadCmdBuffer, vertsLocal, verts, verts.length());
    gpu::cmdMemCpy<Uint32>(uploadCmdBuffer, indicesLocal, indices, indices.length());
    gpu::cmdBarrier(uploadCmdBuffer, Stage::Transfer, Stage::All);
    gpu::queueSubmit(Queue::Main, uploadCmdBuffer);
    
    double nowTs = glfwGetTime();
    Float32 maxDeltaTime = 1.0f / 10.0f;

    gpu::Arena frameArenas[FramesInFlight] = {
        gpu::Arena(sys_alloc), gpu::Arena(sys_alloc), gpu::Arena(sys_alloc)
    };
    for (Ulen i = 0; i < STATIC_LEN(frameArenas); i++) {
        frameArenas[i].init(4 * 1024 * 1024);
    }
    defer({
        for (Ulen i = 0; i < STATIC_LEN(frameArenas); i++) frameArenas[i].destroy();
    });

    Uint64 nextFrame = 1;
    auto frameSem = gpu::semaphoreCreate(0);
    defer(gpu::semaphoreDestroy(frameSem));

    while (!glfwWindowShouldClose(window)) {
        temp_alloc.reset();
        glfwPollEvents();

        Sint32 oldWindowSizeX = windowSizeX;
        Sint32 oldWindowSizeY = windowSizeY;
        glfwGetFramebufferSize(window, &windowSizeX, &windowSizeY);

        if (windowSizeX <= 0 || windowSizeY <= 0) {
            glfwWaitEventsTimeout(0.016);
            continue;
        }

        if (nextFrame > FramesInFlight) {
            gpu::semaphoreWait(frameSem, nextFrame - FramesInFlight);
        }

        if (oldWindowSizeX != windowSizeX || oldWindowSizeY != windowSizeY) {
            gpu::swapchainResize(temp_alloc, windowSizeX, windowSizeY);
        }

        auto swapchainTex = gpu::swapchainAcquireNext();

        double lastTs = nowTs;
        nowTs = glfwGetTime();
        Float32 deltaTime = glm::min(maxDeltaTime, static_cast<Float32>(nowTs - lastTs));

        auto& frameArena = frameArenas[nextFrame % FramesInFlight];
        frameArena.reset();

        auto cmdBuf = gpu::commandsBegin(Queue::Main);

        glm::vec4 color = changingColor(deltaTime);

        gpu::RenderAttachment colorAtt;
        colorAtt.texture = swapchainTex;
        colorAtt.loadOp = LoadOp::Clear;
        colorAtt.storeOp = StoreOp::Store;
        colorAtt.clearColor[0] = color.r;
        colorAtt.clearColor[1] = color.g;
        colorAtt.clearColor[2] = color.b;
        colorAtt.clearColor[3] = color.a;

        gpu::RenderPassDesc renderPassDesc {
            .colorAttachments = Slice<gpu::RenderAttachment>(&colorAtt, 1),
            .renderArea = { swapchainTex.width, swapchainTex.height }
        };

        gpu::cmdBeginRenderPass(cmdBuf, renderPassDesc);
        gpu::cmdBindGraphicsPipeline(cmdBuf, pipeline);

        // --- NOUVEAU PARADIGME ---
        // On configure les états de Culling, Topology et Blending DYNAMIQUEMENT !
        
        gpu::cmdSetRasterizerState(cmdBuf, CullMode::None, FrontFace::CounterClockwise, PrimitiveTopology::TriangleList);
        
        gpu::BlendState blendState {
            .enable = false,
            // Même si désactivé, on passe un write mask valide (très important !)
            .colorWriteMask = 0b1111 // Ecriture RGBA (R=1, G=2, B=4, A=8 => 15)
        };
        gpu::cmdSetBlendState(cmdBuf, blendState);

        gpu::DepthState depthState {
            .mode = DepthFlags::None,
            .compare = CompareOp::Always
        };
        gpu::cmdSetDepthState(cmdBuf, depthState);

        // --- FIN DU NOUVEAU PARADIGME ---

        struct VertData {
            Uint64 vertsAddress; 
        };
        auto vertsData = frameArena.alloc<VertData>();
        vertsData->vertsAddress = vertsLocal.gpu; 

        gpu::cmdDrawIndexedInstanced(cmdBuf, vertsData, gpu::RawPtr{}, indicesLocal, 3, 1);

        gpu::cmdEndRenderPass(cmdBuf);

        gpu::cmdAddSignalSemaphore(cmdBuf, frameSem, nextFrame);
        gpu::queueSubmit(Queue::Main, cmdBuf);
        gpu::swapchainPresent(Queue::Main, frameSem, nextFrame);
        
        nextFrame++;
    }

    gpu::waitIdle();
    return 0;
}

//#include <GLFW/glfw3.h>
//#include <stdio.h>
//#include <math.h>
//#include "ctl/allocator.hpp"
//#include "macro_utils.hpp"
//#include "glm/glm.hpp"
//#include "glm/gtc/constants.hpp"
//#include "gpu/gpu.hpp"
//
//using namespace ctl;
//
//constexpr Uint32 SCREEN_WIDTH  = 800;
//constexpr Uint32 SCREEN_HEIGHT = 600;
//constexpr Uint32 FramesInFlight = 3;
//
//static void errorCallback(Sint32 error, const char *description) {
//    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
//}
//
//glm::vec4 changingColor(Float32 deltaTime) {
//    static float t = 0.0f;
//    t = fmodf(t + deltaTime * 1.7f, glm::pi<float>() * 2.0f);
//
//    glm::vec4 colorA{ 0.2f, 0.2f, 0.2f, 1.0f };
//    glm::vec4 colorB{ 0.4f, 0.4f, 0.4f, 1.0f };
//    
//    float factor = sinf(t) * 0.5f + 0.5f;
//    return glm::mix(colorA, colorB, factor);
//}
//
//Sint32 main() {
//    SystemAllocator sys_alloc;
//    TemporaryAllocator temp_alloc{sys_alloc};
//
//    glfwSetErrorCallback(errorCallback);
//
//    if (!glfwInit()) return 1;
//    defer(glfwTerminate());
//
//    if (!glfwVulkanSupported()) return 1;
//
//    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
//    GLFWwindow *window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Triangle (No Graphics API)", nullptr, nullptr);
//    if (!window) return 1;
//    defer(glfwDestroyWindow(window));
//
//    Sint32 windowSizeX = SCREEN_WIDTH;
//    Sint32 windowSizeY = SCREEN_HEIGHT;
//
//    if (!gpu::init(temp_alloc, window)) {
//        fprintf(stderr, "Failed to init vulkan\n");
//        return 1;
//    }
//    defer(gpu::shutdown());
//
//    gpu::swapchainInitFromGlfw(temp_alloc, window, FramesInFlight);
//
//    auto vertShader = gpu::shaderCreate(temp_alloc, "shaders/spv/test.vert.spv");
//    auto fragShader = gpu::shaderCreate(temp_alloc, "shaders/spv/test.frag.spv");
//    defer(gpu::shaderDestroy(vertShader));
//    defer(gpu::shaderDestroy(fragShader));
//
//    TextureFormat formats[] = { TextureFormat::BGRA8_Unorm };
//    gpu::PipelineDesc pipelineDesc {
//        .vs = vertShader,
//        .fs = fragShader,
//        .colorFormats = ctl::Slice<TextureFormat>{formats},
//        .topology = PrimitiveTopology::TriangleList,
//    };
//    auto pipeline = gpu::graphicsPipelineCreate(temp_alloc, pipelineDesc);
//    defer(gpu::graphicsPipelineDestroy(pipeline));
//
//    struct Vertex {
//        glm::vec4 pos;
//        glm::vec4 col;
//    };
//    
//    auto initArena = gpu::Arena(sys_alloc);
//    defer(initArena.destroy());
//    initArena.init();
//
//    gpu::SlicePtr<Vertex> verts = initArena.alloc<Vertex>(3);
//    verts[0].pos = { -0.5f,  0.5f, 0.0f, 1.0f }; verts[0].col = { 1.0f, 0.0f, 0.0f, 1.0f };
//    verts[1].pos = {  0.0f, -0.5f, 0.0f, 1.0f }; verts[1].col = { 0.0f, 1.0f, 0.0f, 1.0f };
//    verts[2].pos = {  0.5f,  0.5f, 0.0f, 1.0f }; verts[2].col = { 0.0f, 0.0f, 1.0f, 1.0f };
//    
//    gpu::SlicePtr<Uint32> indices = initArena.alloc<Uint32>(3);
//    indices[0] = 0; indices[1] = 2; indices[2] = 1;
//
//    auto vertsLocal = gpu::memAlloc<Vertex>(3, Memory::GPU);
//    defer(gpu::memFree(vertsLocal));
//    auto indicesLocal = gpu::memAlloc<Uint32>(3, Memory::GPU);
//    defer(gpu::memFree(indicesLocal));
//
//    auto uploadCmdBuffer = gpu::commandsBegin(Queue::Main);
//    gpu::cmdMemCpy<Vertex>(uploadCmdBuffer, vertsLocal, verts, verts.length());
//    gpu::cmdMemCpy<Uint32>(uploadCmdBuffer, indicesLocal, indices, indices.length());
//    gpu::cmdBarrier(uploadCmdBuffer, Stage::Transfer, Stage::All);
//    gpu::queueSubmit(Queue::Main, uploadCmdBuffer);
//    
//    double nowTs = glfwGetTime();
//    Float32 maxDeltaTime = 1.0f / 10.0f;
//
//    gpu::Arena frameArenas[FramesInFlight] = {
//        gpu::Arena(sys_alloc), gpu::Arena(sys_alloc), gpu::Arena(sys_alloc)
//    };
//    for (Ulen i = 0; i < STATIC_LEN(frameArenas); i++) {
//        frameArenas[i].init(4 * 1024 * 1024);
//    }
//    defer({
//        for (Ulen i = 0; i < STATIC_LEN(frameArenas); i++) frameArenas[i].destroy();
//    });
//
//    Uint64 nextFrame = 1;
//    auto frameSem = gpu::semaphoreCreate(0);
//    defer(gpu::semaphoreDestroy(frameSem));
//
//    while (!glfwWindowShouldClose(window)) {
//        temp_alloc.reset();
//        glfwPollEvents();
//
//        Sint32 oldWindowSizeX = windowSizeX;
//        Sint32 oldWindowSizeY = windowSizeY;
//        glfwGetFramebufferSize(window, &windowSizeX, &windowSizeY);
//
//        if (windowSizeX <= 0 || windowSizeY <= 0) {
//            glfwWaitEventsTimeout(0.016);
//            continue;
//        }
//
//        if (nextFrame > FramesInFlight) {
//            gpu::semaphoreWait(frameSem, nextFrame - FramesInFlight);
//        }
//
//        auto swapchainTex = gpu::swapchainAcquireNext();
//
//        double lastTs = nowTs;
//        nowTs = glfwGetTime();
//        Float32 deltaTime = glm::min(maxDeltaTime, static_cast<Float32>(nowTs - lastTs));
//
//        auto& frameArena = frameArenas[nextFrame % FramesInFlight];
//        frameArena.reset();
//
//        auto cmdBuf = gpu::commandsBegin(Queue::Main);
//
//        glm::vec4 color = changingColor(deltaTime);
//
//        gpu::RenderAttachment colorAtt;
//        colorAtt.texture = swapchainTex;
//        colorAtt.loadOp = LoadOp::Clear;
//        colorAtt.storeOp = StoreOp::Store;
//        colorAtt.clearColor[0] = color.r;
//        colorAtt.clearColor[1] = color.g;
//        colorAtt.clearColor[2] = color.b;
//        colorAtt.clearColor[3] = color.a;
//
//        gpu::RenderPassDesc renderPassDesc {
//            .colorAttachments = Slice<gpu::RenderAttachment>(&colorAtt, 1),
//            .renderArea = { swapchainTex.width, swapchainTex.height }
//        };
//
//        gpu::cmdBeginRenderPass(cmdBuf, renderPassDesc);
//        gpu::cmdBindGraphicsPipeline(cmdBuf, pipeline);
//
//        struct VertData {
//            Uint64 vertsAddress; 
//        };
//        auto vertsData = frameArena.alloc<VertData>();
//        vertsData->vertsAddress = vertsLocal.gpu; 
//
//        gpu::cmdDrawIndexedInstanced(cmdBuf, vertsData, gpu::RawPtr{}, indicesLocal, 3, 1);
//
//        gpu::cmdEndRenderPass(cmdBuf);
//
//        gpu::cmdAddSignalSemaphore(cmdBuf, frameSem, nextFrame);
//        gpu::queueSubmit(Queue::Main, cmdBuf);
//        gpu::swapchainPresent(Queue::Main, frameSem, nextFrame);
//        
//        nextFrame++;
//    }
//
//    gpu::waitIdle();
//    return 0;
//}
