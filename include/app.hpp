#pragma once

#include <cstdint>
#include <chrono>
#include <iostream>

#include "window.hpp"
#include "instance.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "object.hpp"
#include "camera.hpp"
#include "keyboard_controller.hpp"
#include "buffmanager.hpp"
#include "descriptors.hpp"
#include "lod.hpp"

namespace VKEngine
{

constexpr uint32_t GLOBAL_UBO_LIGHT_COUNT = 2u;

struct GlobalUbo
{
    glm::mat4 projectionViewMatrix{1.f};
    // xyz = normalized direction to light, w = intensity
    glm::vec4 lights[GLOBAL_UBO_LIGHT_COUNT] = {
        // Two top-down directional lights only.
        glm::vec4(glm::normalize(glm::vec3(0.20f, -1.0f, 0.15f)), 1.10f),
        glm::vec4(glm::normalize(glm::vec3(-0.15f, -1.0f, -0.20f)), 0.75f),
    };
};

// Structure to track which objects belong to which segment
struct SegmentRange {
    size_t startIndex;  // Index of first object in segment
    size_t endIndex;    // Index after last object in segment (exclusive)
    std::string name;   // Segment name (e.g., "BOS_H_3", "BOS_I_4")
};

class App final
{
    VKWindow::Window               window_;
    VKInstance::Instance         instance_;
    VKDevice::Device               device_;
    VKRenderer::Renderer         renderer_;

    //  oreder matters
    std::unique_ptr<VKDescriptors::DescriptorPool> globalPool {};
    std::vector<VKObject::Object>                       objects_;
    std::vector<SegmentRange>                     segmentRanges_;  // Track object ranges for each segment
    
    // Storage buffer for object matrices (shared across all objects)
    std::vector<std::unique_ptr<VKBuffmanager::Buffmanager>> objectMatricesBuffers_;
    
    // Storage buffer for materials (shared across all objects)
    std::vector<std::unique_ptr<VKBuffmanager::Buffmanager>> materialsBuffers_;
    std::vector<VKModel::Material> allMaterials_;  // All materials from all models

#ifdef USE_MESH_SHADING
    // Per-frame, per-object: buffer of visible meshlet indices (layout: count at [0], then indices). Used for meshlet culling.
    std::vector<std::vector<std::unique_ptr<VKBuffmanager::Buffmanager>>> visibleMeshletBuffers_;
    std::vector<uint32_t> visibleMeshletCounts_;  // size = objects_.size(), filled each frame
    // Per-object runtime LOD level history (for hysteresis and temporal stability).
    std::vector<uint32_t> runtimeLodLevels_;
#endif

    // Integration checkpoint for LoD rollout (phase-based stop-and-verify).
    VKLod::RuntimeSettings lodSettings_{};

public:
    App() : 
        window_{VKWindow::DEFAULT_WIDTH, 
                VKWindow::DEFAULT_HEIGHT, 
                VKWindow::DEFAULT_WINDOW_NAME},
        instance_{window_}, device_{instance_}, renderer_ {window_, device_}
    {
        loadObjects();

#ifdef USE_MESH_SHADING
        // For mesh shading: UBO, textures, storage buffers for meshlets, object matrices, and materials
        globalPool = VKDescriptors::DescriptorPool::Builder(device_).setMaxSets (VKSwapchain::MAX_FRAMES_IN_FLIGHT * (objects_.size() + 1))  //  max count of descriptor SETS which can be allocated in the future 
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT)
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size())
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * (objects_.size() * 5 + 2)).build();  // 5 per object (meshlets + visible list) + object matrices + materials
#else
        globalPool = VKDescriptors::DescriptorPool::Builder(device_).setMaxSets (VKSwapchain::MAX_FRAMES_IN_FLIGHT * (objects_.size() + 1))  //  max count of descriptor SETS which can be allocated in the future 
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT)  //  add number of descriptors of certain type in pool
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size())
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * 2).build();  // 2 storage buffers: object matrices + materials
#endif
    }
    ~App()= default;

    void run();

private:
    void loadObjects();
    
};

}   //  end of VKEngine class
