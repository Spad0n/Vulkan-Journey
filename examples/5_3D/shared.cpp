#include "shared.hpp"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <stdio.h>

Input G_INPUT;

glm::mat4 matrix4PerspectiveF32(Float32 fovy, Float32 aspect, Float32 near_plane, Float32 far_plane, bool flip_z_axis) {
    Float32 tan_half_fovy = std::tan(fovy * 0.5f);
    glm::mat4 m(0.0f);
    m[0][0] = 1.0f / (aspect * tan_half_fovy);
    m[1][1] = 1.0f / tan_half_fovy;
    m[2][2] = (flip_z_axis ? -1.0f : 1.0f) * far_plane / (far_plane - near_plane);
    m[2][3] = flip_z_axis ? -1.0f : 1.0f;
    m[3][2] = -(far_plane * near_plane) / (far_plane - near_plane);
    return m;
}

bool loadSceneGltf(const char* path, ctl::Allocator& alloc, Scene& out_scene) {
    printf("Loading scene %s...\n", path);
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    
    if (result != cgltf_result_success) return false;
    
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) return false;

    ctl::Array<Uint32> mesh_start_indices(alloc);
    mesh_start_indices.reserve(data->meshes_count);
    
    for (Ulen i = 0; i < data->meshes_count; ++i) {
        mesh_start_indices.push_back((Uint32)out_scene.meshes.length());
        cgltf_mesh& mesh_gltf = data->meshes[i];

        for (Ulen j = 0; j < mesh_gltf.primitives_count; ++j) {
            cgltf_primitive& prim = mesh_gltf.primitives[j];
            Mesh new_mesh(alloc);
            
            for (Ulen k = 0; k < prim.attributes_count; ++k) {
                cgltf_attribute& attr = prim.attributes[k];
                Ulen count = attr.data->count;
                
                if (attr.type == cgltf_attribute_type_position) {
                    new_mesh.pos.reserve(count);
                    for(Ulen n=0; n<count; ++n) {
                        Float32 v[3]; cgltf_accessor_read_float(attr.data, n, v, 3);
                        new_mesh.pos.push_back(glm::vec4(v[0], v[1], v[2], 0.0f));
                    }
                } else if (attr.type == cgltf_attribute_type_normal) {
                    new_mesh.normals.reserve(count);
                    for(Ulen n=0; n<count; ++n) {
                        Float32 v[3]; cgltf_accessor_read_float(attr.data, n, v, 3);
                        new_mesh.normals.push_back(glm::vec4(v[0], v[1], v[2], 0.0f));
                    }
                } else if (attr.type == cgltf_attribute_type_texcoord) {
                    new_mesh.uvs.reserve(count);
                    for(Ulen n=0; n<count; ++n) {
                        Float32 v[2]; cgltf_accessor_read_float(attr.data, n, v, 2);
                        new_mesh.uvs.push_back(glm::vec2(v[0], v[1]));
                    }
                }
            }
            
            if (prim.indices) {
                new_mesh.indices.reserve(prim.indices->count);
                for (Ulen k = 0; k < prim.indices->count; ++k) {
                    new_mesh.indices.push_back((Uint32)cgltf_accessor_read_index(prim.indices, k));
                }
            }
            out_scene.meshes.push_back(ctl::move(new_mesh));
        }
    }
    
    auto process_node = [&](auto& self, cgltf_node* node, glm::mat4 parent_xform) -> void {
        glm::mat4 local_xform(1.0f);
        cgltf_node_transform_local(node, &local_xform[0][0]);
        glm::mat4 global_xform = parent_xform * local_xform;

        if (node->mesh) {
            Sint32 gltf_mesh_idx = -1;
            for(Ulen i=0; i<data->meshes_count; ++i) {
                if(&data->meshes[i] == node->mesh) { gltf_mesh_idx = (Sint32)i; break; }
            }
            
            if (gltf_mesh_idx != -1) {
                Uint32 start_idx = mesh_start_indices[gltf_mesh_idx];
                
                // mirror effect on axe Z !
                glm::mat4 flip_z(1.0f);
                flip_z[2][2] = -1.0f;

                for (Ulen j = 0; j < node->mesh->primitives_count; ++j) {
                    Instance inst;
                    inst.transform = flip_z * global_xform;
                    inst.meshIdx = start_idx + (Uint32)j; 
                    out_scene.instances.push_back(inst);
                }
            }
        }

        for (Ulen i = 0; i < node->children_count; ++i) {
            self(self, node->children[i], global_xform);
        }
    };

    for (Ulen i = 0; i < data->scene->nodes_count; ++i) {
        process_node(process_node, data->scene->nodes[i], glm::mat4(1.0f));
    }

    cgltf_free(data);
    printf("Loaded %llu meshes and %llu instances.\n", out_scene.meshes.length(), out_scene.instances.length());
    return true;
}

bool handleWindowEvents(GLFWwindow* window) {
    for (Sint32 i = 0; i < 512; ++i) {
        G_INPUT.keys[i].pressed = false;
        G_INPUT.keys[i].released = false;
    }
    G_INPUT.mouseDx = 0;
    G_INPUT.mouseDy = 0;

    glfwPollEvents();
    if (glfwWindowShouldClose(window)) return false;

    Float64 mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (G_INPUT.firstMouse) {
        G_INPUT.lastMouseX = mx;
        G_INPUT.lastMouseY = my;
        G_INPUT.firstMouse = false;
    }
    G_INPUT.mouseDx = (Float32)(mx - G_INPUT.lastMouseX);
    G_INPUT.mouseDy = (Float32)(G_INPUT.lastMouseY - my); 
    G_INPUT.lastMouseX = mx;
    G_INPUT.lastMouseY = my;

    G_INPUT.pressingLeftClick = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    
    auto update_key = [&](Sint32 glfw_key) {
        bool down = glfwGetKey(window, glfw_key) == GLFW_PRESS;
        Key_State& s = G_INPUT.keys[glfw_key];
        if (down && !s.pressing) s.pressed = true;
        if (!down && s.pressing) s.released = true;
        s.pressing = down;
    };

    update_key(GLFW_KEY_W); update_key(GLFW_KEY_S);
    update_key(GLFW_KEY_A); update_key(GLFW_KEY_D);
    update_key(GLFW_KEY_Q); update_key(GLFW_KEY_E);

    return true;
}

glm::mat4 firstPersonCameraView(Float32 delta_time) {
    static glm::vec3 cam_pos = {-7.581631f, 1.1906259f, 0.25928685f};
    static glm::vec2 angle = {1.570796f, 0.3665192f};
    static glm::vec3 cur_vel = {0, 0, 0};

    Float32 mouseSensitivity = glm::radians(0.2f); 

    if (G_INPUT.pressingLeftClick) {
        angle.x += G_INPUT.mouseDx * mouseSensitivity;
        angle.y += G_INPUT.mouseDy * mouseSensitivity;
    }

    const Float32 PI = 3.14159265f;
    while (angle.x < 0) angle.x += 2 * PI;
    while (angle.x > 2 * PI) angle.x -= 2 * PI;
    angle.y = glm::clamp(angle.y, glm::radians(-90.0f), glm::radians(90.0f));

    glm::quat y_rot = glm::angleAxis(angle.y, glm::vec3(-1, 0, 0));
    glm::quat x_rot = glm::angleAxis(angle.x, glm::vec3(0, 1, 0));
    glm::quat cam_rot = x_rot * y_rot;

    Float32 move_speed = 6.0f;
    Float32 move_accel = 300.0f;

    glm::vec3 keyboard_dir_xz(0.0f);
    Float32 keyboard_dir_y = 0.0f;

    if (G_INPUT.pressingLeftClick) {
        keyboard_dir_xz.x = (Float32)((Sint32)G_INPUT.keys[GLFW_KEY_D].pressing - (Sint32)G_INPUT.keys[GLFW_KEY_A].pressing);
        keyboard_dir_xz.z = (Float32)((Sint32)G_INPUT.keys[GLFW_KEY_W].pressing - (Sint32)G_INPUT.keys[GLFW_KEY_S].pressing);
        keyboard_dir_y    = (Float32)((Sint32)G_INPUT.keys[GLFW_KEY_E].pressing - (Sint32)G_INPUT.keys[GLFW_KEY_Q].pressing);

        if (glm::dot(keyboard_dir_xz, keyboard_dir_xz) > 1.0f) {
            keyboard_dir_xz = glm::normalize(keyboard_dir_xz);
        }
        if (glm::abs(keyboard_dir_y) > 1.0f) {
            keyboard_dir_y = glm::sign(keyboard_dir_y);
        }
    }

    glm::vec3 target_vel = keyboard_dir_xz * move_speed;
    target_vel = cam_rot * target_vel;
    target_vel.y += keyboard_dir_y * move_speed;

    auto approach_linear = [](glm::vec3 cur, glm::vec3 target, Float32 delta) {
        glm::vec3 diff = target - cur;
        Float32 dist = glm::length(diff);
        if (dist <= delta) return target;
        return cur + (diff / dist) * delta;
    };

    cur_vel = approach_linear(cur_vel, target_vel, move_accel * delta_time);
    cam_pos += cur_vel * delta_time;

    glm::mat4 view_rot = glm::mat4_cast(glm::normalize(glm::inverse(cam_rot)));
    glm::mat4 view_pos = glm::translate(glm::mat4(1.0f), -cam_pos);
    return view_rot * view_pos;
}
