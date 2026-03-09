function(compile_slang_shader_graphics TARGET_NAME SHADER_SOURCE ENTRY_POINT OUTPUT_NAME)
  set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}")

  add_custom_command(
    OUTPUT ${OUTPUT_FILE}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND slangc "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
            -target spirv
            -entry ${ENTRY_POINT}
            -o ${OUTPUT_FILE}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
    COMMENT "Compiling Slang shader: ${SHADER_SOURCE} (${ENTRY_POINT})"
    VERBATIM
  )

  target_sources(${TARGET_NAME} PRIVATE ${OUTPUT_FILE})
endfunction()

function(compile_slang_shader_compute TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
  set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}")

  add_custom_command(
    OUTPUT ${OUTPUT_FILE}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND slangc "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
            -target spirv
            -o ${OUTPUT_FILE}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
    COMMENT "Compiling Slang shader: ${SHADER_SOURCE} (${ENTRY_POINT})"
    VERBATIM
  )

  target_sources(${TARGET_NAME} PRIVATE ${OUTPUT_FILE})
endfunction()

add_executable(1_triangle 1_triangle/main.cpp)
target_link_libraries(1_triangle PRIVATE project_core)
compile_slang_shader_graphics(1_triangle 1_triangle/shaders/shader.slang vertexMain shader.vert.spv)
compile_slang_shader_graphics(1_triangle 1_triangle/shaders/shader.slang fragmentMain shader.frag.spv)

add_executable(2_compute 2_compute/main.cpp)
target_link_libraries(2_compute PRIVATE project_core)
compile_slang_shader_compute(2_compute 2_compute/shaders/matmul.slang matmul.spv)
compile_slang_shader_compute(2_compute 2_compute/shaders/trace.slang trace.spv)

add_executable(3_textures 3_textures/main.cpp)
target_link_libraries(3_textures PRIVATE project_core)
compile_slang_shader_graphics(3_textures 3_textures/shaders/shader.slang vertexMain textures.vert.spv)
compile_slang_shader_graphics(3_textures 3_textures/shaders/shader.slang fragmentMain textures.frag.spv)
add_custom_command(
  TARGET 3_textures POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/3_textures/textures
          $<TARGET_FILE_DIR:3_textures>/textures
)


add_executable(4_indirect_triangles 4_indirect_triangles/main.cpp)
target_link_libraries(4_indirect_triangles PRIVATE project_core)
compile_slang_shader_graphics(4_indirect_triangles 4_indirect_triangles/shaders/shader.slang vertexMain indirect.vert.spv)
compile_slang_shader_graphics(4_indirect_triangles 4_indirect_triangles/shaders/shader.slang fragmentMain indirect.frag.spv)

add_executable(5_3D 5_3D/main.cpp 5_3D/shared.cpp)
target_link_libraries(5_3D PRIVATE project_core)
compile_slang_shader_graphics(5_3D 5_3D/shaders/shader.slang vertexMain 3D.vert.spv)
compile_slang_shader_graphics(5_3D 5_3D/shaders/shader.slang fragmentMain 3D.frag.spv)
add_custom_command(
  TARGET 5_3D POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/5_3D/assets
          $<TARGET_FILE_DIR:5_3D>/assets
)

