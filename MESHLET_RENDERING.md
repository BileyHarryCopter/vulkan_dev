# Meshlet Rendering Implementation

This document describes the meshlet rendering implementation added to the Vulkan application.

## Overview

Meshlet rendering is a modern GPU rendering technique that uses mesh shaders (task and mesh shaders) to process geometry in small clusters called "meshlets". This implementation adds full support for meshlet-based rendering as an alternative to the traditional vertex/index buffer approach.

## Key Changes

### 1. Model Class (`include/model.hpp`, `src/src/model.cpp`)

**New Structures:**
- `Meshlet` struct: Contains offset and count information for vertices and primitives in a meshlet
- Builder now includes meshlet-related data: `meshlets`, `meshletVertices`, `meshletTriangles`

**New Methods:**
- `generate_meshlets()`: Automatically generates meshlets from the model's vertices and indices
  - Max 64 vertices per meshlet
  - Max 126 triangles per meshlet
  - Groups triangles into optimal clusters
- `createMeshletBuffers()`: Creates GPU buffers for meshlet data
- `draw_meshlets()`: Renders using mesh shaders (vkCmdDrawMeshTasksEXT)
- Buffer accessors for meshlet data

**Key Features:**
- Meshlets are automatically generated when loading models via `createModelfromFile()`
- Maintains backward compatibility - traditional rendering still works

### 2. Pipeline Class (`include/pipeline.hpp`, `src/src/pipeline.cpp`)

**New Features:**
- Support for task and mesh shaders in addition to vertex/fragment shaders
- `createMeshShaderPipeline()`: Creates a graphics pipeline using task/mesh shaders
- Constructor now accepts `useMeshShaders` flag to switch between traditional and mesh shader pipelines
- Proper cleanup for both shader types

**Shader Files:**
- Traditional: `vert.spv`, `frag.spv`
- Mesh shading: `task.spv`, `mesh.spv`, `frag.spv`

### 3. Device Class (`include/device.hpp`, `src/src/device.cpp`, `src/src/logical_device.cpp`)

**Extensions and Features:**
- Added `VK_EXT_MESH_SHADER_EXTENSION_NAME` to device extensions
- Enabled mesh shader features: `VkPhysicalDeviceMeshShaderFeaturesEXT`
- Loaded `vkCmdDrawMeshTasksEXT` function pointer for mesh shader dispatch

### 4. Render System (`include/render_system.hpp`, `src/src/render_system.cpp`)

**New Features:**
- `useMeshShaders` flag to enable meshlet rendering
- Updated pipeline layout to support mesh shader stages
- Push constants work with both traditional and mesh shaders
- `renderObjects()` automatically uses meshlet rendering when:
  - Mesh shaders are enabled
  - Model has meshlets available
- Falls back to traditional rendering otherwise

### 5. App Class (`src/src/app.cpp`)

**Descriptor Set Layout:**
Extended with additional bindings for mesh shaders:
- Binding 0: Uniform Buffer (camera/lighting) - shared with vertex shaders
- Binding 1: Combined Image Sampler (texture)
- Binding 2: Storage Buffer (meshlet descriptors)
- Binding 3: Storage Buffer (meshlet vertex indices)
- Binding 4: Storage Buffer (meshlet triangle indices)
- Binding 5: Storage Buffer (vertex data)

**Configuration:**
- `useMeshShaders` flag on line 77 controls whether to use meshlet rendering
- Set to `false` by default for backward compatibility

### 6. Shaders

**Task Shader (`shader.task`):**
- Entry point for mesh shader pipeline
- Currently implements simple pass-through
- Can be extended for:
  - Frustum culling
  - Occlusion culling
  - LOD selection

**Mesh Shader (`shader.mesh`):**
- Replaces the traditional vertex shader
- Processes meshlets (groups of vertices and triangles)
- Outputs vertices and primitives for rasterization
- Supports:
  - Lighting calculations
  - Texture coordinates
  - Transformation matrices

**Fragment Shader:**
- Unchanged, works with both rendering paths

## Usage

### Enabling Meshlet Rendering

1. Open `src/src/app.cpp`
2. Find line ~77: `bool useMeshShaders = false;`
3. Change to: `bool useMeshShaders = true;`
4. Rebuild and run

### Compiling Shaders

```bash
cd src/src/shader
chmod +x compile.sh
./compile.sh
```

This will compile:
- `shader.vert` → `vert.spv`
- `shader.frag` → `frag.spv`
- `shader.task` → `task.spv` (with Vulkan 1.3 target)
- `shader.mesh` → `mesh.spv` (with Vulkan 1.3 target)

## Technical Details

### Meshlet Generation Algorithm

The meshlet generator groups triangles into clusters with constraints:
- Maximum 64 vertices per meshlet
- Maximum 126 triangles per meshlet
- Maintains vertex reuse within meshlets
- Simple greedy algorithm (can be improved with better spatial locality)

### Memory Layout

**Meshlet Descriptor:**
```cpp
struct Meshlet {
    uint32_t vertexOffset;      // Offset into meshletVertices array
    uint32_t vertexCount;       // Number of vertices in this meshlet
    uint32_t primitiveOffset;   // Offset into meshletTriangles array
    uint32_t primitiveCount;    // Number of triangles in this meshlet
};
```

**Buffers:**
- `meshletbuff_`: Array of Meshlet descriptors
- `meshletverticesbuff_`: Indices into the main vertex buffer
- `meshlettrianglesbuff_`: Triangle indices (local to meshlet)
- `vertexbuff_`: Original vertex data

### Performance Considerations

**Benefits of Meshlet Rendering:**
- Better GPU cache utilization
- Efficient culling at meshlet granularity
- Reduced memory bandwidth
- Enables advanced culling techniques

**Current Implementation:**
- No culling implemented yet (all meshlets are rendered)
- Simple meshlet generation (not optimized for cache locality)
- Storage buffers for all meshlet data

**Future Improvements:**
- Implement frustum culling in task shader
- Optimize meshlet generation for spatial locality
- Add LOD support
- Implement occlusion culling

## Requirements

- Vulkan 1.3 or later
- GPU with mesh shader support (VK_EXT_mesh_shader)
- NVIDIA: RTX 20 series or newer
- AMD: RDNA 2 or newer

## Backward Compatibility

The implementation maintains full backward compatibility:
- Traditional rendering path is still available
- Models work with both rendering paths
- Meshlets are generated automatically but not required
- Set `useMeshShaders = false` to use traditional rendering

## Testing

To verify the implementation:
1. Compile with mesh shaders disabled - should work as before
2. Enable mesh shaders - should see identical rendering results
3. Check for proper meshlet generation in model loading
4. Verify descriptor set bindings are correct

## References

- [Vulkan Mesh Shader Extension](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_mesh_shader.html)
- [NVIDIA Mesh Shader Tutorial](https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/)
- [Meshlet Best Practices](https://developer.nvidia.com/blog/advanced-api-performance-mesh-shaders/)
