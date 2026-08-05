cmake_minimum_required(VERSION 3.16)
project(openxaml_layout CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The layout core is deliberately free of platform dependencies: no WinRT, no
# Windows, no Wine. It is verified against measurements recorded from the real
# runtime, which means it can be built and checked anywhere.
add_library(openxaml_layout STATIC
    # The property system everything else stores its values in.
    src/property.cpp
    src/element.cpp
    src/border.cpp
    src/control.cpp
    src/stack_panel.cpp
    src/grid.cpp
    src/chrome.cpp
    src/canvas.cpp
    src/content_presenter.cpp
    src/geometry.cpp
    src/image.cpp
    src/shape.cpp
    src/icon.cpp
    src/brush.cpp
    src/text.cpp
    # Text measurement needs numbers out of a font file, and those arrive as
    # JSON, so the reader moves into the library with them.
    src/fonts.cpp
    src/json.cpp
    # Resource dictionaries, x:Key and {StaticResource}. In the library rather
    # than beside the markup parser because the resolution rules are worth
    # testing on their own, without a case file and without a layout pass.
    src/resources.cpp
    # Style, Setter and BasedOn. Beside the resource system for the same
    # reason: a style is resolved and merged long before anything is measured,
    # and the rules are worth testing without a case file or a layout pass.
    src/style.cpp
    # x: directives and the table an x:Uid resolves against. Beside the
    # resources for the same reason: what a directive means and what a uid
    # resolves to are rules worth testing without a case file and without a
    # layout pass.
    src/xdirectives.cpp
    src/resw_strings.cpp
)
target_include_directories(openxaml_layout PUBLIC src)

add_executable(measure_cases
    src/measure_cases.cpp
    src/markup.cpp
)
target_link_libraries(measure_cases PRIVATE openxaml_layout)

# What the corpus cannot say. A measurement is a tree of numbers, so it cannot
# distinguish an inherited value from a copied one, or say that an element
# noticed a value move. Those checks live here instead.
enable_testing()
add_executable(property_test
    tests/property_test.cpp
    src/markup.cpp
)
target_link_libraries(property_test PRIVATE openxaml_layout)
add_test(NAME property_test COMMAND property_test)

# What a load *refuses* to do. A case that is supposed to fail has no
# measurement, so the corpus cannot hold the resource system to naming what it
# could not resolve -- this can.
add_executable(resource_tests
    tests/resources_test.cpp
    src/markup.cpp
)
target_link_libraries(resource_tests PRIVATE openxaml_layout)
add_test(NAME resources COMMAND resource_tests)

# Which slot a value came from, which is the entire content of what a style is
# and the one thing a tree of numbers cannot report. Plus what a style load
# refuses, for the same reason the resource tests exist.
add_executable(style_tests
    tests/style_test.cpp
    src/markup.cpp
)
target_link_libraries(style_tests PRIVATE openxaml_layout)
add_test(NAME styles COMMAND style_tests)

# The oracle probe only builds on Windows, so nothing about it can be checked
# here -- except the one part that is pure text handling, which is where it had
# a bug that silently emptied every FontIcon glyph in the corpus. That code
# lives in a dependency-free header for exactly this reason.
add_executable(probe_text_test tests/probe_text_test.cpp)
target_include_directories(probe_text_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../harness)
add_test(NAME probe_text COMMAND probe_text_test)

# Whether a recorded error message is even about the case it was recorded on.
# The last measurement run served eleven failures a description left on the
# thread by an earlier case, and a message naming a resource key that the
# markup never mentions is proof of it. Same reason as above: the probe is
# unbuildable here, the decision is pure text, so the decision lives in a
# header and is checked here.
add_executable(error_hygiene_test tests/error_hygiene_test.cpp)
target_include_directories(error_hygiene_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../harness)
add_test(NAME error_hygiene COMMAND error_hygiene_test)

# What a directive does, and what it refuses to do. A deferred element measures
# nothing by definition, so the corpus cannot hold this to anything: "the
# element is absent" and "the element is there but zero-sized" produce different
# trees, and only a test can say which one this is.
add_executable(xdirective_tests
    tests/xdirectives_test.cpp
    src/markup.cpp
)
target_link_libraries(xdirective_tests PRIVATE openxaml_layout)
add_test(NAME xdirectives COMMAND xdirective_tests)

if(MSVC)
    target_compile_options(openxaml_layout PRIVATE /W4)
    target_compile_options(measure_cases PRIVATE /W4)
    target_compile_options(property_test PRIVATE /W4)
    target_compile_options(resource_tests PRIVATE /W4)
    target_compile_options(style_tests PRIVATE /W4)
    target_compile_options(probe_text_test PRIVATE /W4)
    target_compile_options(error_hygiene_test PRIVATE /W4)
    target_compile_options(xdirective_tests PRIVATE /W4)
else()
    target_compile_options(openxaml_layout PRIVATE -Wall -Wextra)
    target_compile_options(measure_cases PRIVATE -Wall -Wextra)
    target_compile_options(property_test PRIVATE -Wall -Wextra)
    target_compile_options(resource_tests PRIVATE -Wall -Wextra)
    target_compile_options(style_tests PRIVATE -Wall -Wextra)
    target_compile_options(probe_text_test PRIVATE -Wall -Wextra)
    target_compile_options(error_hygiene_test PRIVATE -Wall -Wextra)
    target_compile_options(xdirective_tests PRIVATE -Wall -Wextra)
endif()
