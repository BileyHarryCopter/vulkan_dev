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

#ifdef USE_MESH_SHADING
// Define function pointer type for mesh shader draw command
// This should be defined in Vulkan headers, but define it here for compatibility
#ifndef PFN_vkCmdDrawMeshTasksEXT
typedef void (VKAPI_PTR *PFN_vkCmdDrawMeshTasksEXT)(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
#endif
#endif

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

#ifdef USE_MESH_SHADING
        // Get function pointer for mesh shader draw command once during construction
        vkCmdDrawMeshTasksEXT_ = 
            (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(device_.get_logic(), "vkCmdDrawMeshTasksEXT");
        
        if (!vkCmdDrawMeshTasksEXT_) {
            throw std::runtime_error("vkCmdDrawMeshTasksEXT not available! Device may not support mesh shading.");
        }
        std::cout << "vkCmdDrawMeshTasksEXT function pointer obtained successfully" << std::endl;
#endif
    }

    RenderSystem::~RenderSystem() 
    {
        vkDestroyPipelineLayout(device_.get_logic(), pipelineLayout_, nullptr);
    }

    void RenderSystem::createPipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts) 
    {
        VkPushConstantRange pushConstantRange{};
#ifdef USE_MESH_SHADING
        pushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
#else
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
#endif
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SimplePushConstantData);

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

        //  1) Вынести связывание текстур, засунутых в отдельный массив.
        //  2) Отсечение по видимости. 

#ifdef USE_MESH_SHADING
        if (!vkCmdDrawMeshTasksEXT_) {
            throw std::runtime_error("vkCmdDrawMeshTasksEXT not initialized! Mesh shading may not be supported.");
        }

        if (objects.size() == 0) {
            std::cout << "Warning: No objects to render" << std::endl;
            return;
        }

        std::cout << "Rendering " << objects.size() << " objects with mesh shading" << std::endl;

        for (int object_index = 0; object_index < objects.size(); ++object_index)
        {
            if (object_index >= frameinfo.globaldescriptorsets_.size()) {
                throw std::runtime_error("Descriptor set index out of bounds in renderObjects!");
            }

            SimplePushConstantData push_data{};

            push_data.modelMatrix    =       objects[object_index].transform3D_.mat4();
            push_data.normalMatrix = objects[object_index].transform3D_.normalMatrix();

            vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_, 
                                VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(SimplePushConstantData), &push_data);

            vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                pipelineLayout_, 0, 1, &frameinfo.globaldescriptorsets_[object_index], 0, nullptr);

            // Mesh shaders don't use vertex/index buffers
            // Draw meshlets: one mesh task per meshlet in the model
            // Parameters: commandBuffer, groupCountX, groupCountY, groupCountZ
            uint32_t meshletCount = objects[object_index].model_->getMeshletCount();
            if (meshletCount > 0) {
                vkCmdDrawMeshTasksEXT_(frameinfo.commandbuffer_, meshletCount, 1, 1);
            } else {
                // Fallback: draw one mesh task if no meshlets (shouldn't happen)
                vkCmdDrawMeshTasksEXT_(frameinfo.commandbuffer_, 1, 1, 1);
            }
        }
#else
        for (int object_index = 0; object_index < objects.size(); ++object_index)
        {
            SimplePushConstantData                                         push_data{};

            push_data.modelMatrix    =       objects[object_index].transform3D_.mat4();
            push_data.normalMatrix = objects[object_index].transform3D_.normalMatrix();

            vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_, 
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(SimplePushConstantData), &push_data);

            vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                pipelineLayout_, 0, 1, &frameinfo.globaldescriptorsets_[object_index], 0, nullptr);

            objects[object_index].model_ -> bind(frameinfo.commandbuffer_);
            objects[object_index].model_ -> draw(frameinfo.commandbuffer_);
        }
#endif
    }

}  // namespace lve