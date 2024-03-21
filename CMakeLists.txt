cmake_minimum_required(VERSION 3.8)

project(HW3D
        LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 20)

if (${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    message(FATAL_ERROR "In-source build is forbidden")
endif()

find_package (Threads REQUIRED)
find_package (Vulkan  REQUIRED)
find_package (glfw3   REQUIRED)
find_package (GTest REQUIRED)
enable_testing()

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

include_directories(./include/)

add_subdirectory(src)
