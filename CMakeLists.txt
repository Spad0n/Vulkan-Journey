cmake_minimum_required(VERSION 3.25)

project(Modern-Vulkan
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSION OFF)

find_package(Vulkan REQUIRED)

if(USE_ASAN)
  add_library(asan_config INTERFACE)
  if (MSVC)
    target_compile_options(asan_config INTERFACE /fsanitize=address)
    target_link_options(asan_config INTERFACE /fsanitize=address)
  else()
    target_compile_options(asan_config INTERFACE
      -fsanitize=address
      -fno-omit-frame-pointer
    )
    target_link_options(asan_config INTERFACE -fsanitize=address)
  endif()
  message(STATUS "ASan configuration created")
endif()

if(WIN32)
  foreach(flag_var
          CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
          CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    
    # Remove /GR
    if(${flag_var} MATCHES "/GR")
      string(REGEX REPLACE "/GR" "" ${flag_var} "${${flag_var}}")
    endif()
    
    # Remove /EHsc (ou /EHs)
    if(${flag_var} MATCHES "/EHsc")
      string(REGEX REPLACE "/EHsc" "" ${flag_var} "${${flag_var}}")
    endif()
    if(${flag_var} MATCHES "/EHs")
      string(REGEX REPLACE "/EHs" "" ${flag_var} "${${flag_var}}")
    endif()
    
  endforeach()
endif()

add_subdirectory(external/ctl)
add_subdirectory(external/glm)
add_subdirectory(external/glfw-3.4 glfw)
add_subdirectory(external/volk-1.4.304 volk)
add_subdirectory(external/VulkanMemoryAllocator-3.3.0 vma)

add_library(gpu STATIC
  src/gpu.cpp
  src/volk.cpp
  src/vma.cpp
)

#add_executable(vulkanapp
#  examples/1_triangle/main.cpp
#  src/gpu.cpp
#  src/volk.cpp
#  src/vma.cpp
#)
#

target_link_libraries(gpu PUBLIC
  ctl
  glm
  glfw
  Vulkan::Vulkan
  volk
  GPUOpen::VulkanMemoryAllocator
)

target_include_directories(gpu PUBLIC src)

add_subdirectory(examples)
