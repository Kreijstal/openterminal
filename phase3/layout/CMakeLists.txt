cmake_minimum_required(VERSION 3.16)
project(openxaml_layout CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The layout core is deliberately free of platform dependencies: no WinRT, no
# Windows, no Wine. It is verified against measurements recorded from the real
# runtime, which means it can be built and checked anywhere.
add_library(openxaml_layout STATIC
    src/element.cpp
    src/border.cpp
    src/stack_panel.cpp
    src/grid.cpp
    src/text.cpp
    # Text measurement needs numbers out of a font file, and those arrive as
    # JSON, so the reader moves into the library with them.
    src/fonts.cpp
    src/json.cpp
)
target_include_directories(openxaml_layout PUBLIC src)

add_executable(measure_cases
    src/measure_cases.cpp
    src/markup.cpp
)
target_link_libraries(measure_cases PRIVATE openxaml_layout)

if(MSVC)
    target_compile_options(openxaml_layout PRIVATE /W4)
    target_compile_options(measure_cases PRIVATE /W4)
else()
    target_compile_options(openxaml_layout PRIVATE -Wall -Wextra)
    target_compile_options(measure_cases PRIVATE -Wall -Wextra)
endif()
