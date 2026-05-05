cmake_minimum_required(VERSION 3.20)

project(MetalCPPTutorials LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(METAL_CPP_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/metal-cpp"
    CACHE PATH "Path to Apple's metal-cpp headers")
set(METAL_FRAMEWORKS
    "-framework Foundation"
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework QuartzCore"
    "-framework Metal")

add_executable(MetalCppInOneWeekend
    src/MetalCppInOneWeekend/main.cpp)
target_include_directories(MetalCppInOneWeekend PRIVATE "${METAL_CPP_ROOT}")
target_link_libraries(MetalCppInOneWeekend PRIVATE ${METAL_FRAMEWORKS})

set(BOOK1_DIR "${CMAKE_BINARY_DIR}/MetalCppInOneWeekend")
set(BOOK1_METALLIB "${BOOK1_DIR}/default.metallib")
add_custom_command(
    OUTPUT "${BOOK1_METALLIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BOOK1_DIR}/ModuleCache"
    COMMAND xcrun -sdk macosx metal
            "-fmodules-cache-path=${BOOK1_DIR}/ModuleCache"
            -c "${CMAKE_CURRENT_SOURCE_DIR}/src/MetalCppInOneWeekend/Shaders.metal"
            -o "${BOOK1_DIR}/Shaders.air"
    COMMAND xcrun -sdk macosx metallib
            "${BOOK1_DIR}/Shaders.air"
            -o "${BOOK1_METALLIB}"
    DEPENDS src/MetalCppInOneWeekend/Shaders.metal)
add_custom_target(MetalCppInOneWeekendShaders DEPENDS "${BOOK1_METALLIB}")
add_dependencies(MetalCppInOneWeekend MetalCppInOneWeekendShaders)
target_compile_definitions(MetalCppInOneWeekend PRIVATE
    METALLIB_PATH="${BOOK1_METALLIB}")
set_target_properties(MetalCppInOneWeekend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BOOK1_DIR}")

add_executable(MetalCppTinyRenderer
    src/MetalCppTinyRenderer/main.cpp
    src/MetalCppTinyRenderer/Renderer.cpp)
target_include_directories(MetalCppTinyRenderer PRIVATE "${METAL_CPP_ROOT}")
target_link_libraries(MetalCppTinyRenderer PRIVATE ${METAL_FRAMEWORKS})

set(BOOK2_DIR "${CMAKE_BINARY_DIR}/MetalCppTinyRenderer")
set(BOOK2_METALLIB "${BOOK2_DIR}/default.metallib")
add_custom_command(
    OUTPUT "${BOOK2_METALLIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BOOK2_DIR}/ModuleCache"
    COMMAND xcrun -sdk macosx metal
            "-fmodules-cache-path=${BOOK2_DIR}/ModuleCache"
            -c "${CMAKE_CURRENT_SOURCE_DIR}/src/MetalCppTinyRenderer/Shaders.metal"
            -o "${BOOK2_DIR}/Shaders.air"
    COMMAND xcrun -sdk macosx metallib
            "${BOOK2_DIR}/Shaders.air"
            -o "${BOOK2_METALLIB}"
    DEPENDS src/MetalCppTinyRenderer/Shaders.metal)
add_custom_target(MetalCppTinyRendererShaders DEPENDS "${BOOK2_METALLIB}")
add_dependencies(MetalCppTinyRenderer MetalCppTinyRendererShaders)
target_compile_definitions(MetalCppTinyRenderer PRIVATE
    METALLIB_PATH="${BOOK2_METALLIB}")
set_target_properties(MetalCppTinyRenderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BOOK2_DIR}")

add_executable(MetalCppRenderingEngine
    src/MetalCppRenderingEngine/main.cpp)
target_include_directories(MetalCppRenderingEngine PRIVATE "${METAL_CPP_ROOT}")
target_link_libraries(MetalCppRenderingEngine PRIVATE ${METAL_FRAMEWORKS})

set(BOOK3_DIR "${CMAKE_BINARY_DIR}/MetalCppRenderingEngine")
set(BOOK3_METALLIB "${BOOK3_DIR}/default.metallib")
add_custom_command(
    OUTPUT "${BOOK3_METALLIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BOOK3_DIR}/ModuleCache"
    COMMAND xcrun -sdk macosx metal
            "-fmodules-cache-path=${BOOK3_DIR}/ModuleCache"
            -c "${CMAKE_CURRENT_SOURCE_DIR}/src/MetalCppRenderingEngine/Shaders.metal"
            -o "${BOOK3_DIR}/Shaders.air"
    COMMAND xcrun -sdk macosx metallib
            "${BOOK3_DIR}/Shaders.air"
            -o "${BOOK3_METALLIB}"
    DEPENDS src/MetalCppRenderingEngine/Shaders.metal)
add_custom_target(MetalCppRenderingEngineShaders DEPENDS "${BOOK3_METALLIB}")
add_dependencies(MetalCppRenderingEngine MetalCppRenderingEngineShaders)
target_compile_definitions(MetalCppRenderingEngine PRIVATE
    METALLIB_PATH="${BOOK3_METALLIB}")
set_target_properties(MetalCppRenderingEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BOOK3_DIR}")
