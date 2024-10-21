cmake_minimum_required(VERSION 3.26)
project(CFD)

set(CMAKE_CXX_STANDARD 20)

include_directories(
    ${PROJECT_SOURCE_DIR}
    ThirdParty
)

add_library(glad
    ThirdParty/glad/gl.c)

add_executable(Main WIN32
    src/Main.cpp
)
target_link_libraries(Main PRIVATE glad)
target_link_directories(Main PRIVATE lib)