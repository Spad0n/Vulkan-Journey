#ifndef SHARED_HPP
#define SHARED_HPP

#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ctl/array.hpp"
#include "cgltf.h"

using namespace ctl;

struct Mesh {
    Array<glm::vec4> pos;
    Array<glm::vec4> normals;
    Array<glm::vec2> uvs;
    Array<uint32_t> indices;

    Mesh(Allocator& alloc) : pos(alloc), normals(alloc), uvs(alloc), indices(alloc) {}
};

struct Instance {
    glm::mat4 transform;
    Uint32 meshIdx;
};

struct Scene {
    Array<Mesh> meshes;
    Array<Instance> instances;

    Scene(ctl::Allocator& alloc) : meshes(alloc), instances(alloc) {}
};

struct Key_State {
    Bool pressed = false;
    Bool pressing = false;
    Bool released = false;
};

struct Input {
    Bool pressingRightClick = false;
    Bool leftClickPressed = false;
    Key_State keys[512];
    Float32 mouseDx = 0;
    Float32 mouseDy = 0;
    Float64 lastMouseX = 0;
    Float64 lastMouseY = 0;
    Bool firstMouse = true;
};

extern Input G_INPUT;

// Load the scene and apply z mirror
Bool loadSceneGltf(const char* path, ctl::Allocator& alloc, Scene& outScene);

// handle GLFW input
Bool handleWindowEvents(GLFWwindow* window);

// calculate the FPS
glm::mat4 firstPersonCameraView(Float32 deltaTime);

// alternative to glm::perspective in order to work
glm::mat4 matrix4PerspectiveF32(Float32 fovy, Float32 aspect, Float32 nearPlane, Float32 farPlane, Bool flipZAxis = true);

#endif
