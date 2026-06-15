cmake_minimum_required(VERSION 3.16)
project(arena CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)

# All game code is header-only except main.cpp
add_executable(arena src/main.cpp)
target_include_directories(arena PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(arena PRIVATE ${SDL2_LIBRARIES} GLEW::GLEW OpenGL::GL glm::glm)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(arena PRIVATE -O2 -Wall -Wextra)
endif()
