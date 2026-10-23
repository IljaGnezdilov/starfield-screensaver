cmake_minimum_required(VERSION 3.20)
project(Starfield LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep this source list in sync with the cl invocation in build.bat.
add_executable(Starfield WIN32
    src/main.cpp
    src/renderer.cpp
    src/sky.cpp
    src/starfield.cpp
    src/settings.cpp
    res/resource.rc
)

target_include_directories(Starfield PRIVATE src res)
target_compile_definitions(Starfield PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(Starfield PRIVATE /W4 /utf-8 /O2)
    target_link_options(Starfield PRIVATE /SUBSYSTEM:WINDOWS)
endif()

target_link_libraries(Starfield PRIVATE user32 gdi32 winmm shell32 advapi32)

set_target_properties(Starfield PROPERTIES
    OUTPUT_NAME "Starfield"
    SUFFIX ".scr"
)
