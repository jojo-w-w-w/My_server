cmake_minimum_required(VERSION 3.16)

project(MyServer LANGUAGES CXX)

add_executable(server main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(server PRIVATE Threads::Threads)

set_target_properties(
    server PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_options(server PRIVATE -Wall -Wextra)