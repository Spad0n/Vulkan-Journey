let pkgs = import <nixpkgs> { };
in pkgs.mkShell {
  buildInputs = with pkgs; [
    libGL
    vulkan-headers
    vulkan-loader
    vulkan-validation-layers
    vulkan-caps-viewer
    vulkan-memory-allocator
    alsa-lib

    tracy

    cmake
    pkg-config
    valgrind
    doxygen

    shaderc
    shader-slang
    llvmPackages.libcxx

    wayland
    wayland-scanner
    libxkbcommon
    libffi

    sdl3
  ];

  VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";

  VULKAN_SDK = "${pkgs.vulkan-headers}";

  LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${
    with pkgs;
    pkgs.lib.makeLibraryPath [
      alsa-lib
      vulkan-loader
      vulkan-validation-layers
      wayland
      libxkbcommon
    ]
  }";
}
