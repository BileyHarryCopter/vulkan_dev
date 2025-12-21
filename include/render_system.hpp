#pragma once

#include "device.hpp"
#include "object.hpp"
#include "pipeline.hpp"
#include "camera.hpp"
#include "profiler.hpp"

// std
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <thread>
#include <mutex>

namespace VKRenderSystem
{

// Structure to cache precomputed matrices for parallel processing
struct PrecomputedMatrices {
    glm::mat4 modelMatrix{1.f};
    glm::mat4 normalMatrix{1.f};
};

// Structure for storage buffer with object matrices (matches GLSL std430 layout)
struct ObjectMatrices {
    glm::mat4 modelMatrix{1.f};
    glm::mat4 normalMatrix{1.f};
};

struct FrameInfo
{
    int  frameindex_;
    float frametime_;
    VkCommandBuffer                     commandbuffer_;
    VKCamera::Camera&                          camera_;
    std::vector<VkDescriptorSet> globaldescriptorsets_;
    std::vector<PrecomputedMatrices>* precomputedMatrices_ = nullptr;  // Optional precomputed matrices
    VKProfiler::Profiler* profiler_ = nullptr;  // Optional profiler for GPU timing
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;  // Framebuffer for secondary command buffers
};

class RenderSystem 
{

    VKDevice::Device&                       device_;

    std::unique_ptr<VKPipeline::Pipeline> pipeline_;
    VkPipelineLayout                pipelineLayout_;
    VkRenderPass                    renderPass_;  // Store for secondary command buffers

#ifdef USE_MESH_SHADING
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT_ = nullptr;
#endif

    // Thread-local command resources for parallel command recording
    struct ThreadCommandResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> secondaryBuffers;  // One per frame
    };
    std::vector<ThreadCommandResources> threadCommandResources_;
    size_t numThreads_ = 0;
    bool useParallelRecording_ = false;

public:
    RenderSystem(VKDevice::Device &device, VkRenderPass renderPass, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts);
    ~RenderSystem();

    RenderSystem(const RenderSystem &) = delete;
    RenderSystem &operator=(const RenderSystem &) = delete;

    void renderObjects(FrameInfo& frameinfo, std::vector<VKObject::Object> &Objects);
    void enableParallelRecording(bool enable = true);
    bool isParallelRecordingEnabled() const { return useParallelRecording_; }
    void recordSecondaryCommandBuffers(const FrameInfo& frameinfo, const std::vector<VKObject::Object>& objects);
    void executeSecondaryCommandBuffers(const FrameInfo& frameinfo, size_t objectCount);

private:
    void createPipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts);
    void createPipeline(VkRenderPass renderPass);
    void createThreadCommandPools();
    void destroyThreadCommandPools();
    void recordSecondaryCommands(VkCommandBuffer secondaryBuffer, VkRenderPass renderPass, 
                                  const FrameInfo& frameinfo, const std::vector<VKObject::Object>& objects,
                                  size_t startIndex, size_t endIndex);
};

}  // namespace lve
