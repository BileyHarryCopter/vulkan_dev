cmake_minimum_required(VERSION 3.8)

project(HW3D
        LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 20)

if (${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    message(FATAL_ERROR "In-source build is forbidden")
endif()

find_package (Threads REQUIRED)
find_package (Vulkan  REQUIRED)

# Try to find glfw3 using pkg-config first, then fallback to manual search
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GLFW3 glfw3)
    if(GLFW3_FOUND)
        message(STATUS "Found GLFW3 via pkg-config: ${GLFW3_LIBRARIES}")
    endif()
endif()

if(NOT GLFW3_FOUND)
    # Manual search for glfw3
    message(STATUS "Searching for GLFW3 manually...")
    find_path(GLFW3_INCLUDE_DIR GLFW/glfw3.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
    )
    
    find_library(GLFW3_LIBRARY
        NAMES glfw3 glfw
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        /usr/lib/x86_64-linux-gnu
    )
    
    if(GLFW3_INCLUDE_DIR AND GLFW3_LIBRARY)
        set(GLFW3_FOUND TRUE)
        set(GLFW3_INCLUDE_DIRS ${GLFW3_INCLUDE_DIR})
        set(GLFW3_LIBRARIES ${GLFW3_LIBRARY})
        message(STATUS "Found GLFW3 manually: ${GLFW3_LIBRARIES}")
    else()
        # Try find_package as last resort
        find_package(glfw3 QUIET)
        if(glfw3_FOUND)
            set(GLFW3_FOUND TRUE)
            message(STATUS "Found GLFW3 via find_package")
        endif()
    endif()
endif()

if(NOT GLFW3_FOUND)
    message(FATAL_ERROR "GLFW3 not found. Please install libglfw3-dev: sudo apt install libglfw3-dev")
endif()

# Ensure variables are set correctly
if(NOT GLFW3_INCLUDE_DIRS)
    if(GLFW3_INCLUDE_DIR)
        set(GLFW3_INCLUDE_DIRS ${GLFW3_INCLUDE_DIR})
    endif()
endif()

if(NOT GLFW3_LIBRARIES)
    if(GLFW3_LIBRARY)
        set(GLFW3_LIBRARIES ${GLFW3_LIBRARY})
    endif()
endif()

# Find GLM (OpenGL Mathematics library)
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
)

if(NOT GLM_INCLUDE_DIR)
    message(WARNING "GLM not found in standard paths. Trying to find via pkg-config...")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLM QUIET glm)
    endif()
endif()

if(NOT GLM_INCLUDE_DIR AND NOT GLM_FOUND)
    message(FATAL_ERROR "GLM not found! Please install libglm-dev:\n"
                        "  Ubuntu/Debian: sudo apt install libglm-dev")
else()
    if(GLM_INCLUDE_DIR)
        set(GLM_INCLUDE_DIRS ${GLM_INCLUDE_DIR})
        message(STATUS "Found GLM: ${GLM_INCLUDE_DIRS}")
    elseif(GLM_FOUND)
        message(STATUS "Found GLM via pkg-config: ${GLM_INCLUDE_DIRS}")
    endif()
endif()

find_package (GTest REQUIRED)
enable_testing()

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Option to enable mesh shading support
option(USE_MESH_SHADING "Enable mesh shading pipeline (requires VK_EXT_mesh_shader)" OFF)

include_directories(./include/)

add_subdirectory(src)
