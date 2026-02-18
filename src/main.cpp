#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include "ctl/allocator.hpp"
#include "macro_utils.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "gpu.hpp"

using namespace ctl;

constexpr Uint32 SCREEN_WIDTH  = 1024;
constexpr Uint32 SCREEN_HEIGHT = 768;

static void errorCallback(Sint32 error, const char* description) {
    fprintf(stderr, "GLFW Error: %d %s\n", error, description);
}

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

const Vertex TRIANGLE_DATA[] = {
    { { 0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // Bas milieu, Rouge
    { { 0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // Haut droite, Vert
    { {-0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }  // Haut gauche, Bleu
};

struct DrawData {
    glm::mat4 transform;
    Uint64 vertexBufferAddress;
};

#if 0
Sint32 main() {
    SystemAllocator sys;
    TemporaryAllocator temp_alloc{sys};

    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    if (!glfwVulkanSupported()) return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Hello Vulkan", nullptr, nullptr);
    if (!window) return 1;
    defer(glfwDestroyWindow(window));

    gpu::init(temp_alloc, window, SCREEN_WIDTH, SCREEN_HEIGHT);
    defer(gpu::shutdown());

    gpu::Pipeline pipeline = gpu::createGraphicsPipeline(temp_alloc, "shaders/spv/shader.vert.spv", "shaders/spv/shader.frag.spv");
    defer(gpu::destroyPipeline(pipeline));

    while (!glfwWindowShouldClose(window)) {
        temp_alloc.reset();
        glfwPollEvents();

        VkCommandBuffer cmd = gpu::beginFrame();
        if (cmd == VK_NULL_HANDLE) continue;

        auto alloc = gpu::alloc<Vertex>(3);

        Vertex* vPtr = alloc.as<Vertex>();
        memcpy(vPtr, TRIANGLE_DATA, sizeof(Vertex) * 3);

        gpu::setPipeline(cmd, pipeline);

        gpu::pushConstants(cmd, &alloc.cpu, sizeof(DrawData));

        gpu::draw(cmd, 3, 1, 0, 0);

        gpu::endFrame();
    }

    gpu::waitIdle();

    return 0;
}
#else
Sint32 main() {
    SystemAllocator sys;
    TemporaryAllocator temp_alloc{sys};

    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) return 1;
    defer(glfwTerminate());

    if (!glfwVulkanSupported()) return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Hello Vulkan", nullptr, nullptr);
    if (!window) return 1;
    defer(glfwDestroyWindow(window));

    gpu::init(temp_alloc, window, SCREEN_WIDTH, SCREEN_HEIGHT);
    defer(gpu::shutdown());

    gpu::Pipeline pipeline = gpu::createGraphicsPipeline(temp_alloc, "shaders/spv/shader.vert.spv", "shaders/spv/shader.frag.spv");
    defer(gpu::destroyPipeline(pipeline));

    while (!glfwWindowShouldClose(window)) {
        temp_alloc.reset();
        glfwPollEvents();

        Float32 time = (Float32)glfwGetTime();
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 0.0f, 1.0f));

        Float32 aspect = (Float32)SCREEN_WIDTH / (Float32)SCREEN_HEIGHT;
        glm::mat4 projection = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);
        glm::mat4 finalTransform = model;

        VkCommandBuffer cmd = gpu::beginFrame();
        if (cmd == VK_NULL_HANDLE) continue;

        auto rootAlloc = gpu::alloc<DrawData>();
        DrawData* data = rootAlloc.as<DrawData>();

        data->transform = finalTransform;
        auto vertexAlloc = gpu::alloc<Vertex>(3);
        memcpy(vertexAlloc.as<Vertex>(), TRIANGLE_DATA, sizeof(Vertex) * 3);
        data->vertexBufferAddress = vertexAlloc.gpu;

        gpu::setPipeline(cmd, pipeline);

        gpu::pushAddress(cmd, rootAlloc.gpu);

        gpu::draw(cmd, 3, 1, 0, 0);

        gpu::endFrame();
    }

    gpu::waitIdle();

    return 0;
}
#endif
