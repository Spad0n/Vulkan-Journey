cmake_minimum_required(VERSION 3.25)

project(Modern-Vulkan
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSION OFF)

find_package(Vulkan REQUIRED)

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

add_executable(vulkanapp
  src/main.cpp
  src/gpu.cpp
)

target_link_libraries(vulkanapp PRIVATE
  ctl
  glm
  glfw
  Vulkan::Vulkan
  volk
  #GPUOpen::VulkanMemoryAllocator
)
