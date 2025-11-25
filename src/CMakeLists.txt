aux_source_directory(./src SRC_LIST)

add_executable(VKSOURCES ${SRC_LIST})

set (TINY_OBJ_LOADER ${CMAKE_CURRENT_SOURCE_DIR}/../tinyobjloader/)
set (STB_IMAGE_IMPL  ${CMAKE_CURRENT_SOURCE_DIR}/../stb_image/)

target_include_directories (VKSOURCES
                            PUBLIC ${GLFW3_INCLUDE_DIRS}
                            PUBLIC ${Vulkan_INCLUDE_DIRS}
                            PUBLIC ${GLM_INCLUDE_DIRS}
                            PUBLIC ${CMAKE_THREAD_LIBS_INIT}
                            PUBLIC ${TINY_OBJ_LOADER}
                            PUBLIC ${STB_IMAGE_IMPL}
)

set (tinyobjloader_SRC 
    ${TINY_OBJ_LOADER}/tinyobjloader.h
    ${TINY_OBJ_LOADER}/tinyobjloader.cc
)

target_link_libraries (VKSOURCES ${GLFW3_LIBRARIES} vulkan dl X11 Xxf86vm Xrandr Xi ${tinyobjloader_SRC})

# Pass USE_MESH_SHADING flag to the code
target_compile_definitions(VKSOURCES PRIVATE $<$<BOOL:${USE_MESH_SHADING}>:USE_MESH_SHADING>)

# Find glslc compiler for shaders, fallback to glslang if not found
find_program(GLSLC glslc
    PATHS
    /usr/bin
    /usr/local/bin
    $ENV{VULKAN_SDK}/bin
    $ENV{HOME}/VulkanSDK/*/bin
    /opt/homebrew/bin
)

# If glslc not found, try to use glslang as fallback
if(NOT GLSLC)
    find_program(GLSLANG glslang
        PATHS
        /usr/bin
        /usr/local/bin
    )
    
    if(GLSLANG)
        set(GLSLC ${GLSLANG})
        # Use -V for Vulkan SPIR-V generation, --target-env vulkan1.3 for mesh shader support
        # Store flags as list for proper CMake handling
        set(GLSLC_FLAGS "-V" "--target-env" "vulkan1.3")
        message(STATUS "glslc not found, using glslang as fallback: ${GLSLC}")
    else()
        message(FATAL_ERROR "Neither glslc nor glslang found! Please install:\n"
                            "  Ubuntu/Debian: sudo apt install glslang-tools\n"
                            "  Or install Vulkan SDK from https://vulkan.lunarg.com/")
    endif()
else()
    set(GLSLC_FLAGS "")  # glslc doesn't need -G flag
    message(STATUS "Found glslc: ${GLSLC}")
endif()

# a part which necessary for compiling .vert and .frag files
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/frag.spv
    COMMAND ${GLSLC} ${GLSLC_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/shader.frag -o ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/shader.frag
    VERBATIM)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/vert.spv
    COMMAND ${GLSLC} ${GLSLC_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/shader.vert -o ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/vert.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/shader.vert
    VERBATIM)

# Compile mesh shader if USE_MESH_SHADING is enabled
if(USE_MESH_SHADING)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/mesh.spv
        COMMAND ${GLSLC} ${GLSLC_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/mesh.mesh -o ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/mesh.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/mesh.mesh
        VERBATIM)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/frag_mesh.spv
        COMMAND ${GLSLC} ${GLSLC_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/shader_mesh.frag -o ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/frag_mesh.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/shader_mesh.frag
        VERBATIM)
    # Make sure mesh shaders are built before the executable
    add_custom_target(mesh_shader_target DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/mesh.spv ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/frag_mesh.spv)
    add_dependencies(VKSOURCES mesh_shader_target)
endif()
