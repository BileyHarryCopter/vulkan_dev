#pragma once

#include <iostream>
#include <chrono>

#include "window.hpp"
#include "instance.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "object.hpp"
#include "camera.hpp"
#include "keyboard_controller.hpp"
#include "buffmanager.hpp"
#include "descriptors.hpp"

namespace VKEngine
{

struct GlobalUbo
{
    glm::mat4 projectionViewMatrix {1.f};
    glm::vec3 directionToLight = glm::normalize(glm::vec3{0.5, -1.0, 0.5});
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

public:
    App() : 
        window_{VKWindow::DEFAULT_WIDTH, 
                VKWindow::DEFAULT_HEIGHT, 
                VKWindow::DEFAULT_WINDOW_NAME},
        instance_{window_}, device_{instance_}, renderer_ {window_, device_}
    {
        loadObjects();

#ifdef USE_MESH_SHADING
        // For mesh shading: UBO, textures, storage buffers for meshlets, and object matrices
        globalPool = VKDescriptors::DescriptorPool::Builder(device_).setMaxSets (VKSwapchain::MAX_FRAMES_IN_FLIGHT * (objects_.size() + 1))  //  max count of descriptor SETS which can be allocated in the future 
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT)
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size())
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * (objects_.size() * 4 + 1)).build();  // 4 storage buffers per object + 1 for object matrices
#else
        globalPool = VKDescriptors::DescriptorPool::Builder(device_).setMaxSets (VKSwapchain::MAX_FRAMES_IN_FLIGHT * (objects_.size() + 1))  //  max count of descriptor SETS which can be allocated in the future 
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT)  //  add number of descriptors of certain type in pool
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size())
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT).build();  // 1 storage buffer for object matrices
#endif
    }
    ~App()= default;

    void run();

private:
    void loadObjects();
    
};

}   //  end of VKEngine class
