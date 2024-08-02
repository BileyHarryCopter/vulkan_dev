#include "render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// std
#include <array>
#include <cassert>
#include <stdexcept>

namespace VKRenderSystem {

struct SimplePushConstantData 
{
    glm::mat4  modelMatrix{1.f};
    glm::mat4 normalMatrix{1.f};
};

    RenderSystem::RenderSystem(VKDevice::Device &device, VkRenderPass renderPass, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts): device_{device} 
    {
        createPipelineLayout(descriptorSetLayouts);

        createPipeline(renderPass);
    }

    RenderSystem::~RenderSystem() 
    {
        vkDestroyPipelineLayout(device_.get_logic(), pipelineLayout_, nullptr);
    }

    void RenderSystem::createPipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts) 
    {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SimplePushConstantData);

        std::cout << "Size of descriptorSetLayouts: " << descriptorSetLayouts.size() << std::endl;
        std::cout << "Data of descriptorSetLayouts: " << descriptorSetLayouts.data() << std::endl;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType                  =      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount         = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts            =                        descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount =                                                  1;
        pipelineLayoutInfo.pPushConstantRanges    =                                 &pushConstantRange;

        if (vkCreatePipelineLayout(device_.get_logic(), &pipelineLayoutInfo, nullptr, &pipelineLayout_) !=
            VK_SUCCESS)
            throw std::runtime_error("failed to create pipeline layout!");        
    }

    void RenderSystem::createPipeline(VkRenderPass renderPass) 
    {
        assert(pipelineLayout_ != nullptr && "Cannot create pipeline before pipeline layout");

        VKPipeline::PipelineConfigInfo pipelineConfig{};
        VKPipeline::Pipeline::defaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout_;

        pipeline_ = std::make_unique<VKPipeline::Pipeline>(device_, pipelineConfig);
    }

    void RenderSystem::renderObjects(FrameInfo& frameinfo, std::vector<VKObject::Object> &objects)
    {
        pipeline_->bind(frameinfo.commandbuffer_);

        std::cout << "Number of descriptors: " << frameinfo.globaldescriptorsets_.size() << std::endl;

        vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                pipelineLayout_, 0, 1, &frameinfo.globaldescriptorsets_[0], 0, nullptr);

        for (int object_index = 0; object_index < objects.size(); ++object_index)
        {
            SimplePushConstantData                                         push_data{};

            push_data.modelMatrix    =       objects[object_index].transform3D_.mat4();
            push_data.normalMatrix = objects[object_index].transform3D_.normalMatrix();

            vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_, 
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(SimplePushConstantData), &push_data);

            std::cout << "obj index: " << object_index << std::endl;
            vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                pipelineLayout_, 1, 1, &frameinfo.globaldescriptorsets_[object_index + 1], 0, nullptr);

            objects[object_index].model_ -> bind(frameinfo.commandbuffer_);
            objects[object_index].model_ -> draw(frameinfo.commandbuffer_);
        }
    }

}  // namespace lve