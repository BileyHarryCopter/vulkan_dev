#!/bin/bash
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
# Mesh shader compilation is handled by CMake when USE_MESH_SHADING is enabled
# glslc mesh.mesh -o mesh.spv