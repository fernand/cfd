cmake_minimum_required(VERSION 3.26)
project(CFD)

set(CMAKE_CXX_STANDARD 20)

include_directories(
    ${PROJECT_SOURCE_DIR}
    ThirdParty
)

add_library(glad
    ThirdParty/glad/gl.c)

add_library(ImGui
    ThirdParty/imgui/imgui_demo.cpp
    ThirdParty/imgui/imgui_draw.cpp
    ThirdParty/imgui/imgui_tables.cpp
    ThirdParty/imgui/imgui_widgets.cpp
    ThirdParty/imgui/imgui.cpp
    ThirdParty/imgui/imgui_impl_opengl3.cpp
    ThirdParty/imgui/imgui_impl_glfw.cpp
    ThirdParty/imgui/imgui_stdlib.cpp
)

add_executable(Main WIN32
    src/Main.cpp
)
target_link_libraries(Main PRIVATE glad)
target_link_directories(Main PRIVATE lib)
target_link_libraries(Main PRIVATE ImGui)