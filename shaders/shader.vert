#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
    vec3 position;
    vec3 color;
};

layout(buffer_reference, scalar) buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant, scalar) uniform DrawData {
    mat4 transform;
    VertexBuffer vBufferPtr;
} root;

layout(location = 0) out vec3 outColor;

void main() {
    Vertex v = root.vBufferPtr.vertices[gl_VertexIndex];

    gl_Position = root.transform * vec4(v.position, 1.0);
    outColor = v.color;
}
