set(CTL_SOURCES
  allocator.cpp
  cpprt.cpp
  file.cpp
  pool.cpp
  slab.cpp
  stream.cpp
  string.cpp
  system.cpp
  unicode.cpp
)

add_library(ctl ${CTL_SOURCES})

target_include_directories(ctl
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(ctl PUBLIC cxx_std_20)

# in case there is some bugs
option(CTL_NO_STDLIBCXX "Disable C++ standard library" OFF)

if(MSVC)

  # On windows, you should put this on the root project or else you see warnings
  #foreach(flag_var
  #        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
  #        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
  #  
  #  # Remove /GR
  #  if(${flag_var} MATCHES "/GR")
  #    string(REGEX REPLACE "/GR" "" ${flag_var} "${${flag_var}}")
  #  endif()
  #  
  #  # Remove /EHsc (ou /EHs)
  #  if(${flag_var} MATCHES "/EHsc")
  #    string(REGEX REPLACE "/EHsc" "" ${flag_var} "${${flag_var}}")
  #  endif()
  #  if(${flag_var} MATCHES "/EHs")
  #    string(REGEX REPLACE "/EHs" "" ${flag_var} "${${flag_var}}")
  #  endif()
  #  
  #endforeach()

  target_compile_options(ctl
    PUBLIC
    /EHs-	# disable exceptions
    /GR-	# disable RTTI
    /wd4530	# disbale warning "C++ exception handler used..." if necessary
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # GCC / Clang
  target_compile_options(ctl PUBLIC
    -fno-exceptions
    -fno-rtti
  )
  if (CTL_NO_STDLIBCXX)
    target_link_options(ctl PUBLIC "-nostdlib++")
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_compile_options(ctl PUBLIC
    -nostdlibinc  # no system headers, but only use builtin clang
    -msimd128     # active <wasm_simd128.h> for allocator.cpp
    -mbulk-memory # active memory.fill / memory.copy
  )
  #target_compile_definitions(ctl PUBLIC CTL_HOST_PLATFORM_WASM)
endif()

if(USE_ASAN AND TARGET asan_config)
  target_link_libraries(ctl PRIVATE asan_config)
  target_compile_definitions(ctl PRIVATE CTL_USE_ASAN=1)
  target_compile_definitions(ctl PRIVATE CTL_CFG_USE_MALLOC=1)
endif()
