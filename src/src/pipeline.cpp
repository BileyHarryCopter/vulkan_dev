#include "pipeline.hpp"

#include "model.hpp"

namespace VKPipeline
{

    Pipeline::Pipeline(VKDevice::Device& device, const PipelineConfigInfo& configInfo)
                        : device_{device.get_logic()}
    {
        createGraphicsPipeline(configInfo);
    }

    Pipeline::~Pipeline()
    {
        vkDestroyShaderModule  (device_, fragshadermodule_, nullptr);
        vkDestroyShaderModule  (device_, vertshadermodule_, nullptr);
        vkDestroyPipeline      (device_, graphicspipeline_, nullptr);
    }


    VkShaderModule createShaderModule(const std::vector<char>& code, const VkDevice& device)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    =    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize =                                    code.size();
        createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
            throw std::runtime_error("failed to create shader module!");

        return shaderModule;
    }

    void Pipeline::createGraphicsPipeline(const PipelineConfigInfo& configInfo)
    {
        assert(configInfo.pipelineLayout != VK_NULL_HANDLE &&
                "Cannot create graphics pipeline: no pipelineLayout provided in configInfo");
        assert(configInfo.renderPass != VK_NULL_HANDLE &&
                "Cannot create graphics pipeline: no renderPass provided in configInfo");


        auto vertShaderCode = Service::readfile(VERT_SHADER_FILE_NAME);
        auto fragShaderCode = Service::readfile(FRAG_SHADER_FILE_NAME);

        vertshadermodule_ = createShaderModule(vertShaderCode, device_);
        fragshadermodule_ = createShaderModule(fragShaderCode, device_);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage  =                          VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module =                                   vertshadermodule_;
        vertShaderStageInfo.pName  =                                              "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage  =                        VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module =                                   fragshadermodule_;
        fragShaderStageInfo.pName  =                                              "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};


        auto   binding_descriptions =   VKModel::Model::Vertex::get_binding_descriptions();
        auto attribute_descriptions = VKModel::Model::Vertex::get_attribute_descriptions();
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount   = static_cast<uint32_t>(  binding_descriptions.size());
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
        vertexInputInfo.pVertexBindingDescriptions      =                          binding_descriptions.data();
        vertexInputInfo.pVertexAttributeDescriptions    =                        attribute_descriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology                      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable        =                            VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount =                                                     1;
        viewportState.scissorCount  =                                                     1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType      = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable        =                                      VK_FALSE;
        rasterizer.rasterizerDiscardEnable =                                      VK_FALSE;
        rasterizer.polygonMode             =                          VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth               =                                          1.0f;
        rasterizer.cullMode                =                         VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace               =                       VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable         =                                      VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable                                   = VK_FALSE;
        multisampling.rasterizationSamples                     = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable    =                                             VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable     =                                                 VK_FALSE;
        colorBlending.logicOp           =                                         VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount   =                                                        1;
        colorBlending.pAttachments      =                                    &colorBlendAttachment;
        colorBlending.blendConstants[0] =                                                     0.0f;
        colorBlending.blendConstants[1] =                                                     0.0f;
        colorBlending.blendConstants[2] =                                                     0.0f;
        colorBlending.blendConstants[3] =                                                     0.0f;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo {};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.depthTestEnable  =                                         VK_TRUE;
        depthStencilInfo.depthWriteEnable =                                         VK_TRUE;
        depthStencilInfo.depthCompareOp   =                              VK_COMPARE_OP_LESS;
        depthStencilInfo.depthBoundsTestEnable =                                   VK_FALSE;
        depthStencilInfo.stencilTestEnable     =                                   VK_FALSE;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount =          static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates    =                                 dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          =                                               2;
        pipelineInfo.pStages             =                                    shaderStages;
        pipelineInfo.pVertexInputState   =                                &vertexInputInfo;
        pipelineInfo.pInputAssemblyState =                   &configInfo.inputAssemblyInfo;
        pipelineInfo.pViewportState      =                        &configInfo.viewportInfo;
        pipelineInfo.pRasterizationState =                   &configInfo.rasterizationInfo;
        pipelineInfo.pMultisampleState   =                     &configInfo.multisampleInfo;
        pipelineInfo.pColorBlendState    =                      &configInfo.colorBlendInfo;
        pipelineInfo.pDepthStencilState  =                    &configInfo.depthStencilInfo;
        pipelineInfo.pDynamicState       =                                   &dynamicState;
        pipelineInfo.layout              =                       configInfo.pipelineLayout;
        pipelineInfo.renderPass          =                           configInfo.renderPass;
        pipelineInfo.subpass             =                              configInfo.subpass;
        pipelineInfo.basePipelineHandle  =                                  VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicspipeline_) != VK_SUCCESS)
            throw std::runtime_error("failed to create graphics pipeline!");
    }

    void Pipeline::bind(VkCommandBuffer commandBuffer)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicspipeline_);
    }

    void Pipeline::defaultPipelineConfigInfo(PipelineConfigInfo& configInfo) 
    {
        configInfo.inputAssemblyInfo.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        configInfo.inputAssemblyInfo.topology =                         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        configInfo.inputAssemblyInfo.primitiveRestartEnable =                                      VK_FALSE;

        configInfo.viewportInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        configInfo.viewportInfo.viewportCount =                                                     1;
        configInfo.viewportInfo.pViewports    =                                               nullptr;
        configInfo.viewportInfo.scissorCount  =                                                     1;
        configInfo.viewportInfo.pScissors     =                                               nullptr;

        configInfo.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        configInfo.rasterizationInfo.depthClampEnable        =                                 VK_FALSE;
        configInfo.rasterizationInfo.rasterizerDiscardEnable =                                 VK_FALSE;
        configInfo.rasterizationInfo.polygonMode             =                     VK_POLYGON_MODE_FILL;
        configInfo.rasterizationInfo.lineWidth               =                                     1.0f;
        configInfo.rasterizationInfo.cullMode                =                        VK_CULL_MODE_NONE;
        configInfo.rasterizationInfo.frontFace               =                  VK_FRONT_FACE_CLOCKWISE;
        configInfo.rasterizationInfo.depthBiasEnable         =                                 VK_FALSE;
        configInfo.rasterizationInfo.depthBiasConstantFactor =                                     0.0f;   // Optional
        configInfo.rasterizationInfo.depthBiasClamp          =                                     0.0f;   // Optional
        configInfo.rasterizationInfo.depthBiasSlopeFactor    =                                     0.0f;   // Optional

        configInfo.multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        configInfo.multisampleInfo.sampleShadingEnable = VK_FALSE;
        configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        configInfo.multisampleInfo.minSampleShading = 1.0f;           // Optional
        configInfo.multisampleInfo.pSampleMask = nullptr;             // Optional
        configInfo.multisampleInfo.alphaToCoverageEnable = VK_FALSE;  // Optional
        configInfo.multisampleInfo.alphaToOneEnable = VK_FALSE;       // Optional

        configInfo.colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        configInfo.colorBlendAttachment.blendEnable = VK_FALSE;
        configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
        configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
        configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
        configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
        configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
        configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional

        configInfo.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;
        configInfo.colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;  // Optional
        configInfo.colorBlendInfo.attachmentCount = 1;
        configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;
        configInfo.colorBlendInfo.blendConstants[0] = 0.0f;  // Optional
        configInfo.colorBlendInfo.blendConstants[1] = 0.0f;  // Optional
        configInfo.colorBlendInfo.blendConstants[2] = 0.0f;  // Optional
        configInfo.colorBlendInfo.blendConstants[3] = 0.0f;  // Optional

        configInfo.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
        configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
        configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
        configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
        configInfo.depthStencilInfo.minDepthBounds = 0.0f;  // Optional
        configInfo.depthStencilInfo.maxDepthBounds = 1.0f;  // Optional
        configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
        configInfo.depthStencilInfo.front = {};  // Optional
        configInfo.depthStencilInfo.back = {};   // Optional

        configInfo.dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
        configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
        configInfo.dynamicStateInfo.flags = 0;
    }

}   //  end of VKPipeline namespace
