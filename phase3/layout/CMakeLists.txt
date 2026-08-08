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
    # Framework event registration, and the two events a layout pass raises.
    # Beside the property system because it is the other half of what a
    # DependencyObject offers a caller that is not measuring anything.
    src/events.cpp
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
    src/scroll_viewer.cpp
    src/basic_controls.cpp
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
    # Wave 3: observable bindings, visual states/storyboards and generic.xaml
    # control templates. Wave 4: virtualized items, navigation and overlays.
    src/binding.cpp
    src/visual_state.cpp
    src/default_styles.cpp
    src/advanced_controls.cpp
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

# What a directive does, and what it refuses to do. The corpus settled deferral
# -- a runtime load ignores x:Load and realises the element -- but it cannot
# settle a refusal, and the refusals are most of this file: markup that fails to
# load has no measurement to compare against.
add_executable(xdirective_tests
    tests/xdirectives_test.cpp
    src/markup.cpp
)
target_link_libraries(xdirective_tests PRIVATE openxaml_layout)
add_test(NAME xdirectives COMMAND xdirective_tests)

# Two rules the corpus states in one number each: the layout-rounding tie-break
# and ContentControl's content alignment. Both are measured, so this is not a
# second authority -- it is the rule written out as arithmetic, so that a change
# to either one is a deliberate edit rather than a case that quietly moves.
add_executable(layout_rules_tests
    tests/layout_rules_test.cpp
)
target_link_libraries(layout_rules_tests PRIVATE openxaml_layout)
add_test(NAME layout_rules COMMAND layout_rules_tests)

# Which elements take part in layout, and what the ones that do not report
# instead. A recorded tree cannot tell "reports nothing but renders its
# specified size" from "was never measured": both are zeros in the desired
# column, and only one of them is zero in the actual column for a reason the
# markup shows.
add_executable(layout_participation_tests tests/layout_participation_test.cpp)
target_link_libraries(layout_participation_tests PRIVATE openxaml_layout)
add_test(NAME layout_participation COMMAND layout_participation_tests)

# Which way a measured text height is turned into a whole number. The corpus
# has exactly two rows that can tell ceiling from rounding and they disagree,
# so the rule that separates them is stated here instead.
add_executable(text_measure_tests tests/text_measure_test.cpp)
target_link_libraries(text_measure_tests PRIVATE openxaml_layout)
add_test(NAME text_measure COMMAND text_measure_tests)

add_executable(wave3_tests tests/wave3_test.cpp src/markup.cpp)
target_link_libraries(wave3_tests PRIVATE openxaml_layout)
add_test(NAME wave3 COMMAND wave3_tests)

add_executable(wave4_tests tests/wave4_test.cpp src/markup.cpp)
target_link_libraries(wave4_tests PRIVATE openxaml_layout)
add_test(NAME wave4 COMMAND wave4_tests)

# Which of the two scrollbars a viewer's desired size grew for, and where the
# padding went. A recorded tree reports the viewer's answer and its content's,
# with nothing in between: the presenter the padding moves and the bars that
# hold their track open are template parts the oracle never names.
add_executable(scroll_viewer_tests
    tests/scroll_viewer_test.cpp
    src/markup.cpp
)
target_link_libraries(scroll_viewer_tests PRIVATE openxaml_layout)
add_test(NAME scroll_viewer COMMAND scroll_viewer_tests)

# Which rule produced a level 4 number. The corpus pins the numbers and cannot
# pin the rules: a line height read off the wrong family and one read off the
# right one agree whenever the two fonts happen to match, and Segoe UI's
# fallback list is full of families that do not. These hold each composition
# rule against metrics invented for the purpose, where they disagree by
# construction.
add_executable(l4_composition_tests
    tests/l4_composition_test.cpp
    src/markup.cpp
)
target_link_libraries(l4_composition_tests PRIVATE openxaml_layout)
add_test(NAME l4_composition COMMAND l4_composition_tests)

add_executable(windows_xaml_completion_tests
    tests/windows_xaml_completion_test.cpp
    src/markup.cpp
)
target_link_libraries(windows_xaml_completion_tests PRIVATE openxaml_layout)
add_test(NAME windows_xaml_completion COMMAND windows_xaml_completion_tests)

# What the harvested Terminal subtrees pin, and the shape of the tree they are
# reported in. A recorded tree says which nodes the probe's walk reached, and
# that is a fact about the walk rather than about layout -- so it is stated
# here, beside the rules the level 7 numbers pin.
add_executable(terminal_subtree_tests
    tests/terminal_subtree_test.cpp
    src/markup.cpp
)
target_link_libraries(terminal_subtree_tests PRIVATE openxaml_layout)
add_test(NAME terminal_subtree COMMAND terminal_subtree_tests)

# The property system and the event system, which no measurement can see. A
# recorded tree is a tree of numbers: it cannot say which slot a value came
# from, that a callback ran before another one, or that a token took its
# handler back off. Those are the whole content of what the ABI now reaches.
add_executable(dependency_property_tests
    tests/dependency_property_test.cpp
)
target_link_libraries(dependency_property_tests PRIVATE openxaml_layout)
add_test(NAME dependency_property COMMAND dependency_property_tests)

if(MSVC)
    target_compile_options(openxaml_layout PRIVATE /W4)
    target_compile_options(measure_cases PRIVATE /W4)
    target_compile_options(property_test PRIVATE /W4)
    target_compile_options(resource_tests PRIVATE /W4)
    target_compile_options(style_tests PRIVATE /W4)
    target_compile_options(probe_text_test PRIVATE /W4)
    target_compile_options(error_hygiene_test PRIVATE /W4)
    target_compile_options(xdirective_tests PRIVATE /W4)
    target_compile_options(layout_rules_tests PRIVATE /W4)
    target_compile_options(layout_participation_tests PRIVATE /W4)
    target_compile_options(text_measure_tests PRIVATE /W4)
    target_compile_options(wave3_tests PRIVATE /W4)
    target_compile_options(wave4_tests PRIVATE /W4)
    target_compile_options(scroll_viewer_tests PRIVATE /W4)
    target_compile_options(l4_composition_tests PRIVATE /W4)
    target_compile_options(windows_xaml_completion_tests PRIVATE /W4)
    target_compile_options(terminal_subtree_tests PRIVATE /W4)
else()
    target_compile_options(openxaml_layout PRIVATE -Wall -Wextra)
    target_compile_options(measure_cases PRIVATE -Wall -Wextra)
    target_compile_options(property_test PRIVATE -Wall -Wextra)
    target_compile_options(resource_tests PRIVATE -Wall -Wextra)
    target_compile_options(style_tests PRIVATE -Wall -Wextra)
    target_compile_options(probe_text_test PRIVATE -Wall -Wextra)
    target_compile_options(error_hygiene_test PRIVATE -Wall -Wextra)
    target_compile_options(xdirective_tests PRIVATE -Wall -Wextra)
    target_compile_options(layout_rules_tests PRIVATE -Wall -Wextra)
    target_compile_options(layout_participation_tests PRIVATE -Wall -Wextra)
    target_compile_options(text_measure_tests PRIVATE -Wall -Wextra)
    target_compile_options(wave3_tests PRIVATE -Wall -Wextra)
    target_compile_options(wave4_tests PRIVATE -Wall -Wextra)
    target_compile_options(scroll_viewer_tests PRIVATE -Wall -Wextra)
    target_compile_options(l4_composition_tests PRIVATE -Wall -Wextra)
    target_compile_options(windows_xaml_completion_tests PRIVATE -Wall -Wextra)
    target_compile_options(terminal_subtree_tests PRIVATE -Wall -Wextra)
    target_compile_options(dependency_property_tests PRIVATE -Wall -Wextra)
endif()
