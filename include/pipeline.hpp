#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <string>

#include "utility.hpp"
#include "device.hpp"
#include "swapchain.hpp"

namespace VKPipeline
{

const std::string VERT_SHADER_FILE_NAME = "../../src/src/shader/vert.spv";
const std::string FRAG_SHADER_FILE_NAME = "../../src/src/shader/frag.spv";

struct PipelineConfigInfo 
{
    PipelineConfigInfo()                                     = default;
    PipelineConfigInfo(const PipelineConfigInfo&)            =  delete;
    PipelineConfigInfo& operator=(const PipelineConfigInfo&) =  delete;

    VkPipelineViewportStateCreateInfo                     viewportInfo;
    VkPipelineInputAssemblyStateCreateInfo           inputAssemblyInfo;
    VkPipelineRasterizationStateCreateInfo           rasterizationInfo;
    VkPipelineMultisampleStateCreateInfo               multisampleInfo;
    VkPipelineColorBlendAttachmentState           colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo                 colorBlendInfo;

    VkPipelineDepthStencilStateCreateInfo             depthStencilInfo;

    std::vector<VkDynamicState>                    dynamicStateEnables;
    VkPipelineDynamicStateCreateInfo                  dynamicStateInfo;
    VkPipelineLayout                          pipelineLayout = nullptr;
    VkRenderPass                                  renderPass = nullptr;
    uint32_t                                               subpass = 0;
};


class Pipeline final
{

    VkDevice                   device_ = VK_NULL_HANDLE;
    VkRenderPass           renderpass_ = VK_NULL_HANDLE;

    VkShaderModule   vertshadermodule_ = VK_NULL_HANDLE;
    VkShaderModule   fragshadermodule_ = VK_NULL_HANDLE;

    VkPipeline       graphicspipeline_ = VK_NULL_HANDLE;


    void createGraphicsPipeline(const PipelineConfigInfo& configInfo);

public:

    Pipeline (VKDevice::Device& device, const PipelineConfigInfo& configInfo);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkRenderPass get_renderpass() { return renderpass_; }

    void bind(VkCommandBuffer commandBuffer);

    static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
};

}   //  end of VKPipeline namespace
