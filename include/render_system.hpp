#pragma once

#include "device.hpp"
#include "object.hpp"
#include "pipeline.hpp"
#include "camera.hpp"

// std
#include <memory>
#include <vector>

namespace VKRenderSystem
{

struct FrameInfo
{
    int  frameindex_;
    float frametime_;
    VkCommandBuffer       commandbuffer_;
    VKCamera::Camera&            camera_;
    VkDescriptorSet globaldescriptorset_;
};

class RenderSystem 
{

    VKDevice::Device&                       device_;

    std::unique_ptr<VKPipeline::Pipeline> pipeline_;
    VkPipelineLayout                pipelineLayout_;

public:
    RenderSystem(VKDevice::Device &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
    ~RenderSystem();

    RenderSystem(const RenderSystem &) = delete;
    RenderSystem &operator=(const RenderSystem &) = delete;

    void renderObjects(FrameInfo& frameinfo, std::vector<VKObject::Object> &Objects);

private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipeline(VkRenderPass renderPass);

};

}  // namespace lve
