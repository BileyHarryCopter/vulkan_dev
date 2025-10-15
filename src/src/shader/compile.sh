#!/bin/bash
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
glslc --target-env=vulkan1.3 -fshader-stage=task shader.task -o task.spv
glslc --target-env=vulkan1.3 -fshader-stage=mesh shader.mesh -o mesh.spv
