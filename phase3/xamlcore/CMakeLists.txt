cmake_minimum_required(VERSION 3.16)
project(openxaml_xbf_reader CXX)

# This reader is the ABI implementation used by openxaml.dll. Its public
# headers deliberately use Win32/WinRT types, so a native Linux configure is
# not a supported approximation of this target. Fail at configure time with
# the required toolchain instead of reaching an incidental windows.h error.
if(NOT WIN32 OR NOT MINGW)
    message(FATAL_ERROR
        "phase3/xamlcore requires the MinGW-w64 Windows toolchain; configure "
        "with -DCMAKE_TOOLCHAIN_FILE=<repo>/phase2/toolchains/x64-mingw-static.cmake")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(openxaml_xbf STATIC src/xbf.cpp src/xbf_object.cpp)
target_include_directories(openxaml_xbf PUBLIC src)

add_executable(xbf_reader_test tests/xbf_reader_test.cpp)
target_link_libraries(xbf_reader_test PRIVATE openxaml_xbf)

enable_testing()
add_test(NAME xbf_reader COMMAND xbf_reader_test)

if(MSVC)
    target_compile_options(openxaml_xbf PRIVATE /W4 /WX)
    target_compile_options(xbf_reader_test PRIVATE /W4 /WX)
else()
    target_compile_options(openxaml_xbf PRIVATE -Wall -Wextra -Werror)
    target_compile_options(xbf_reader_test PRIVATE -Wall -Wextra -Werror)
endif()
