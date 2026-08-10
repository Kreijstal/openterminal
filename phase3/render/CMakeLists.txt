cmake_minimum_required(VERSION 3.16)
project(openxaml_render CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The layout core, which decides every number this paints. Added rather than
# duplicated: the whole claim of this directory is that it draws what layout
# already verified, so it has to be the same layout.
if(NOT TARGET openxaml_layout)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../layout
                     ${CMAKE_CURRENT_BINARY_DIR}/layout)
endif()

# The render core is as free of platform dependencies as the layout core is:
# no WinRT, no Windows, no GDI. Deciding what to paint, where, and in what
# colour is arithmetic over the arranged tree, and it is checked on the Linux
# test path. Only the glyph imagery is the platform's -- see gdi/.
add_library(openxaml_render STATIC
    src/display_list.cpp
    src/scene.cpp
    src/surface.cpp
    src/cpu_raster_backend.cpp
    src/case_runner.cpp
    src/external_surface_reader.cpp
)
target_include_directories(openxaml_render PUBLIC src)
target_link_libraries(openxaml_render PUBLIC openxaml_layout)

add_executable(render_cases
    src/render_cases.cpp
    ../layout/src/markup.cpp
)
target_link_libraries(render_cases PRIVATE openxaml_render)

add_executable(render_oracle_cases
    src/render_oracle_cases.cpp
    ../layout/src/markup.cpp
)
target_link_libraries(render_oracle_cases PRIVATE openxaml_render)

enable_testing()

# What a dump cannot say. A refused case paints nothing, so the pixels cannot
# show that the refusal was the right answer rather than a renderer that
# quietly did nothing.
add_executable(render_tests
    tests/render_test.cpp
    ../layout/src/markup.cpp
)
target_link_libraries(render_tests PRIVATE openxaml_render)
add_test(NAME render COMMAND render_tests)

add_executable(scene_tests tests/scene_test.cpp)
target_link_libraries(scene_tests PRIVATE openxaml_render)
add_test(NAME scene COMMAND scene_tests)

add_executable(cpu_raster_backend_tests tests/cpu_raster_backend_test.cpp)
target_link_libraries(cpu_raster_backend_tests PRIVATE openxaml_render)
add_test(NAME cpu_raster_backend COMMAND cpu_raster_backend_tests)

# A runtime text provider may refuse one leaf without erasing independently
# renderable ancestors or inventing substitute glyphs. This boundary is plain
# C++ and belongs on the platform-neutral CTest path.
add_executable(runtime_text_refusal_tests tests/runtime_text_refusal_test.cpp)
target_link_libraries(runtime_text_refusal_tests PRIVATE openxaml_render)
add_test(NAME runtime_text_refusal COMMAND runtime_text_refusal_tests)

# DirectComposition is a Windows-only presentation backend. Keep it in a
# separate target so the deterministic render core and its Linux acceptance
# path acquire neither Windows headers nor compositor libraries.
if(WIN32)
    add_library(openxaml_render_dcomp STATIC
        dcomp/dcomp_scene_backend.cpp
    )
    target_include_directories(openxaml_render_dcomp PUBLIC dcomp)
    target_link_libraries(openxaml_render_dcomp
        PUBLIC openxaml_render
        PRIVATE dcomp d3d11 dxgi ole32
    )

    add_executable(dcomp_scene_backend_tests
        dcomp/dcomp_scene_backend_test.cpp
    )
    target_link_libraries(dcomp_scene_backend_tests PRIVATE openxaml_render_dcomp)
    add_test(NAME dcomp_scene_backend COMMAND dcomp_scene_backend_tests)
endif()

if(MSVC)
    target_compile_options(openxaml_render PRIVATE /W4)
    target_compile_options(render_cases PRIVATE /W4)
    target_compile_options(render_oracle_cases PRIVATE /W4)
    target_compile_options(render_tests PRIVATE /W4)
    target_compile_options(scene_tests PRIVATE /W4)
    target_compile_options(cpu_raster_backend_tests PRIVATE /W4)
    target_compile_options(runtime_text_refusal_tests PRIVATE /W4)
    if(TARGET openxaml_render_dcomp)
        target_compile_options(openxaml_render_dcomp PRIVATE /W4)
        target_compile_options(dcomp_scene_backend_tests PRIVATE /W4)
    endif()
else()
    target_compile_options(openxaml_render PRIVATE -Wall -Wextra)
    target_compile_options(render_cases PRIVATE -Wall -Wextra)
    target_compile_options(render_oracle_cases PRIVATE -Wall -Wextra)
    target_compile_options(render_tests PRIVATE -Wall -Wextra)
    target_compile_options(scene_tests PRIVATE -Wall -Wextra)
    target_compile_options(cpu_raster_backend_tests PRIVATE -Wall -Wextra)
    target_compile_options(runtime_text_refusal_tests PRIVATE -Wall -Wextra)
    if(TARGET openxaml_render_dcomp)
        target_compile_options(openxaml_render_dcomp PRIVATE -Wall -Wextra)
        target_compile_options(dcomp_scene_backend_tests PRIVATE -Wall -Wextra)
    endif()
endif()
