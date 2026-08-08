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
    src/surface.cpp
    src/case_runner.cpp
)
target_include_directories(openxaml_render PUBLIC src)
target_link_libraries(openxaml_render PUBLIC openxaml_layout)

add_executable(render_cases
    src/render_cases.cpp
    ../layout/src/markup.cpp
)
target_link_libraries(render_cases PRIVATE openxaml_render)

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

if(MSVC)
    target_compile_options(openxaml_render PRIVATE /W4)
    target_compile_options(render_cases PRIVATE /W4)
    target_compile_options(render_tests PRIVATE /W4)
else()
    target_compile_options(openxaml_render PRIVATE -Wall -Wextra)
    target_compile_options(render_cases PRIVATE -Wall -Wextra)
    target_compile_options(render_tests PRIVATE -Wall -Wextra)
endif()
