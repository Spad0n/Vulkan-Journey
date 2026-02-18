@echo off

set GLSLC=glslc
set INPUT_DIR=shaders
set OUTPUT_DIR=shaders\spv

if not exist %OUTPUT_DIR% (
   mkdir %OUTPUT_DIR%
)

for %%f in (%INPUT_DIR%\*.vert %INPUT_DIR%\*.frag %INPUT_DIR%\*.comp) do (
    echo Compiling %%f ...
    %GLSLC% %%f --target-env=vulkan1.3 -o %OUTPUT_DIR%\%%~nf%%~xf.spv
)

echo Done.
pause
