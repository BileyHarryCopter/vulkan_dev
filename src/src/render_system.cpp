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
#include <thread>
#include <algorithm>

#ifdef USE_MESH_SHADING
// Define function pointer type for mesh shader draw command
// This should be defined in Vulkan headers, but define it here for compatibility
#ifndef PFN_vkCmdDrawMeshTasksEXT
typedef void (VKAPI_PTR *PFN_vkCmdDrawMeshTasksEXT)(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
#endif
#endif

namespace VKRenderSystem {

// Push constant contains object index and material index
struct SimplePushConstantData 
{
    uint32_t objectIndex{0};
    uint32_t materialIndex{0};
    uint32_t padding[2];  // Padding to align to 16 bytes (required by Vulkan)
};

// Thread-safe frame data structure for parallel command recording
struct ThreadSafeFrameData {
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<PrecomputedMatrices> precomputedMatrices;
    VkFramebuffer framebuffer;
    bool hasPrecomputedMatrices;
#ifdef USE_MESH_SHADING
    std::vector<uint32_t>* visibleMeshletCounts = nullptr;
#endif
};

    RenderSystem::RenderSystem(VKDevice::Device &device, VkRenderPass renderPass, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts): device_{device}, renderPass_{renderPass}
    {
        createPipelineLayout(descriptorSetLayouts);

        createPipeline(renderPass);
        
        // Initialize parallel recording (disabled by default, enable if needed)
        numThreads_ = std::max(1u, std::thread::hardware_concurrency());
        useParallelRecording_ = false;  // Can be enabled later via enableParallelRecording()

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
        destroyThreadCommandPools();
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
        pushConstantRange.size = sizeof(SimplePushConstantData);  // 16 bytes (object index + material index + padding)

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

    void RenderSystem::renderObjects(FrameInfo& frameinfo, std::vector<VKObject::Object> &objects,
                                    const std::vector<uint32_t>* visibleIndices)
    {
        const size_t numToRender = visibleIndices ? visibleIndices->size() : objects.size();
        if (numToRender == 0 || objects.size() == 0) {
            return;
        }

        // Write draw start timestamp if profiler is available
        if (frameinfo.profiler_ && frameinfo.profiler_->isSupported()) {
            frameinfo.profiler_->writeTimestamp(frameinfo.commandbuffer_, frameinfo.frameindex_,
                                               VKProfiler::TimestampQuery::DRAW_START,
                                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }

        if (useParallelRecording_ && threadCommandResources_.size() > 0) {
            executeSecondaryCommandBuffers(frameinfo, numToRender);
        } else {
            pipeline_->bind(frameinfo.commandbuffer_);

#ifdef USE_MESH_SHADING
            if (!vkCmdDrawMeshTasksEXT_) {
                throw std::runtime_error("vkCmdDrawMeshTasksEXT not initialized! Mesh shading may not be supported.");
            }

            VkDescriptorSet lastBoundDescriptorSet = VK_NULL_HANDLE;

            for (size_t i = 0; i < numToRender; ++i) {
                const uint32_t object_index = visibleIndices ? (*visibleIndices)[i] : static_cast<uint32_t>(i);
                if (object_index >= frameinfo.globaldescriptorsets_.size()) {
                    throw std::runtime_error("Descriptor set index out of bounds in renderObjects!");
                }

                VkDescriptorSet currentDescriptorSet = frameinfo.globaldescriptorsets_[object_index];
                if (currentDescriptorSet != lastBoundDescriptorSet) {
                    vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &currentDescriptorSet, 0, nullptr);
                    lastBoundDescriptorSet = currentDescriptorSet;
                }

                SimplePushConstantData push_data{};
                push_data.objectIndex = object_index;
                push_data.materialIndex = 0;

                vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_,
                                    VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0, sizeof(SimplePushConstantData), &push_data);

                uint32_t meshletCount = (frameinfo.visibleMeshletCounts_ && object_index < frameinfo.visibleMeshletCounts_->size())
                    ? (*frameinfo.visibleMeshletCounts_)[object_index]
                    : objects[object_index].model_->getMeshletCount();
                if (meshletCount > 0) {
                    vkCmdDrawMeshTasksEXT_(frameinfo.commandbuffer_, meshletCount, 1, 1);
                } else {
                    vkCmdDrawMeshTasksEXT_(frameinfo.commandbuffer_, 1, 1, 1);
                }
            }
#else
            VkDescriptorSet lastBoundDescriptorSet = VK_NULL_HANDLE;

            for (size_t i = 0; i < numToRender; ++i) {
                const uint32_t object_index = visibleIndices ? (*visibleIndices)[i] : static_cast<uint32_t>(i);
                VkDescriptorSet currentDescriptorSet = frameinfo.globaldescriptorsets_[object_index];
                if (currentDescriptorSet != lastBoundDescriptorSet) {
                    vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &currentDescriptorSet, 0, nullptr);
                    lastBoundDescriptorSet = currentDescriptorSet;
                }

                const auto& model = objects[object_index].model_;
                uint32_t submeshCount = model->getSubmeshCount();

                if (submeshCount > 0) {
                    for (uint32_t submeshIdx = 0; submeshIdx < submeshCount; ++submeshIdx) {
                        const auto& submeshes = model->getSubmeshes();
                        if (submeshIdx < submeshes.size()) {
                            const auto& submesh = submeshes[submeshIdx];
                            SimplePushConstantData push_data{};
                            push_data.objectIndex = object_index;
                            push_data.materialIndex = submesh.materialIndex;
                            vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_,
                                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                0, sizeof(SimplePushConstantData), &push_data);
                            model->bind(frameinfo.commandbuffer_);
                            model->drawSubmesh(frameinfo.commandbuffer_, submeshIdx);
                        }
                    }
                } else {
                    SimplePushConstantData push_data{};
                    push_data.objectIndex = object_index;
                    push_data.materialIndex = 0;
                    vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_,
                                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                        0, sizeof(SimplePushConstantData), &push_data);
                    model->bind(frameinfo.commandbuffer_);
                    model->draw(frameinfo.commandbuffer_);
                }
            }
#endif
        }

        // Write draw end timestamp if profiler is available
        if (frameinfo.profiler_ && frameinfo.profiler_->isSupported()) {
            frameinfo.profiler_->writeTimestamp(frameinfo.commandbuffer_, frameinfo.frameindex_,
                                               VKProfiler::TimestampQuery::DRAW_END,
                                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }
    }

    void RenderSystem::enableParallelRecording(bool enable)
    {
        useParallelRecording_ = enable;
        if (enable) {
            if (threadCommandResources_.empty()) {
                createThreadCommandPools();
                std::cout << "Parallel command recording enabled with " << numThreads_ << " threads" << std::endl;
            }
        } else {
            destroyThreadCommandPools();
        }
    }

    void RenderSystem::createThreadCommandPools()
    {
        if (threadCommandResources_.size() > 0) {
            destroyThreadCommandPools();  // Clean up existing pools
        }

        threadCommandResources_.clear();
        threadCommandResources_.resize(numThreads_);

        uint32_t queueFamilyIndex = device_.get_indices().get_graphics_value();

        for (size_t threadId = 0; threadId < numThreads_; threadId++) {
            auto& threadRes = threadCommandResources_[threadId];

            // Create command pool for this thread
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = queueFamilyIndex;

            if (vkCreateCommandPool(device_.get_logic(), &poolInfo, nullptr, &threadRes.commandPool) != VK_SUCCESS) {
                throw std::runtime_error("failed to create thread command pool!");
            }

            // Allocate secondary command buffers for each frame
            threadRes.secondaryBuffers.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = threadRes.commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            allocInfo.commandBufferCount = VKSwapchain::MAX_FRAMES_IN_FLIGHT;

            if (vkAllocateCommandBuffers(device_.get_logic(), &allocInfo, threadRes.secondaryBuffers.data()) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate secondary command buffers!");
            }
        }
    }

    void RenderSystem::destroyThreadCommandPools()
    {
        for (auto& threadRes : threadCommandResources_) {
            if (!threadRes.secondaryBuffers.empty() && threadRes.commandPool != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device_.get_logic(), threadRes.commandPool,
                                   static_cast<uint32_t>(threadRes.secondaryBuffers.size()),
                                   threadRes.secondaryBuffers.data());
                threadRes.secondaryBuffers.clear();
            }
            if (threadRes.commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_.get_logic(), threadRes.commandPool, nullptr);
                threadRes.commandPool = VK_NULL_HANDLE;
            }
        }
        threadCommandResources_.clear();
    }

    void RenderSystem::recordSecondaryCommands(VkCommandBuffer secondaryBuffer, VkRenderPass renderPass,
                                                const FrameInfo& frameinfo, const std::vector<VKObject::Object>& objects,
                                                size_t startIndex, size_t endIndex,
                                                const std::vector<uint32_t>* visibleIndices)
    {
        // Safety checks
        if (secondaryBuffer == VK_NULL_HANDLE) {
            throw std::runtime_error("Secondary command buffer is null in recordSecondaryCommands!");
        }
        if (renderPass == VK_NULL_HANDLE) {
            throw std::runtime_error("Render pass is null in recordSecondaryCommands!");
        }
        if (frameinfo.framebuffer_ == VK_NULL_HANDLE) {
            throw std::runtime_error("Framebuffer is null in recordSecondaryCommands!");
        }
        if (startIndex >= objects.size()) {
            return;  // Nothing to record
        }
        if (endIndex < startIndex) {
            return;  // Invalid range
        }

        VkCommandBufferInheritanceInfo inheritanceInfo{};
        inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritanceInfo.renderPass = renderPass;
        inheritanceInfo.subpass = 0;
        inheritanceInfo.framebuffer = frameinfo.framebuffer_;  // Use framebuffer from frameinfo
        inheritanceInfo.occlusionQueryEnable = VK_FALSE;
        inheritanceInfo.queryFlags = 0;
        inheritanceInfo.pipelineStatistics = 0;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = &inheritanceInfo;

        if (vkBeginCommandBuffer(secondaryBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin secondary command buffer!");
        }

        pipeline_->bind(secondaryBuffer);

        const size_t rangeCount = endIndex - startIndex;
        for (size_t r = 0; r < rangeCount; r++) {
            const uint32_t object_index = visibleIndices
                ? (*visibleIndices)[startIndex + r]
                : static_cast<uint32_t>(startIndex + r);
            if (object_index >= frameinfo.globaldescriptorsets_.size() || !objects[object_index].model_) {
                continue;
            }

            SimplePushConstantData push_data{};
            push_data.objectIndex = object_index;
            push_data.materialIndex = 0;

#ifdef USE_MESH_SHADING
            vkCmdPushConstants(secondaryBuffer, pipelineLayout_,
                              VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);
            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout_, 0, 1, &frameinfo.globaldescriptorsets_[object_index], 0, nullptr);
            uint32_t meshletCount = (frameinfo.visibleMeshletCounts_ && object_index < frameinfo.visibleMeshletCounts_->size())
                ? (*frameinfo.visibleMeshletCounts_)[object_index]
                : objects[object_index].model_->getMeshletCount();
            if (meshletCount > 0) {
                vkCmdDrawMeshTasksEXT_(secondaryBuffer, meshletCount, 1, 1);
            }
#else
            vkCmdPushConstants(secondaryBuffer, pipelineLayout_,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);
            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout_, 0, 1, &frameinfo.globaldescriptorsets_[object_index], 0, nullptr);
            objects[object_index].model_->bind(secondaryBuffer);
            objects[object_index].model_->draw(secondaryBuffer);
#endif
        }

        if (vkEndCommandBuffer(secondaryBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to end secondary command buffer!");
        }
    }

    // Internal thread-safe function for parallel command recording
    // This is a helper function, not a method, so it needs to receive all necessary parameters
    static void recordSecondaryCommandsThreadSafe(
        VkCommandBuffer secondaryBuffer, VkRenderPass renderPass,
        const ThreadSafeFrameData& threadData, const std::vector<VKObject::Object>& objects,
        size_t startIndex, size_t endIndex,
        const std::vector<uint32_t>* visibleIndices,
        VKPipeline::Pipeline* pipeline, VkPipelineLayout pipelineLayout
#ifdef USE_MESH_SHADING
        , PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT
#endif
    )
    {
        // Safety checks
        if (secondaryBuffer == VK_NULL_HANDLE) {
            throw std::runtime_error("Secondary command buffer is null in recordSecondaryCommandsThreadSafe!");
        }
        if (renderPass == VK_NULL_HANDLE) {
            throw std::runtime_error("Render pass is null in recordSecondaryCommandsThreadSafe!");
        }
        if (threadData.framebuffer == VK_NULL_HANDLE) {
            throw std::runtime_error("Framebuffer is null in recordSecondaryCommandsThreadSafe!");
        }
        const size_t rangeCount = endIndex > startIndex ? endIndex - startIndex : 0;
        if (rangeCount == 0) {
            return;
        }

        VkCommandBufferInheritanceInfo inheritanceInfo{};
        inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritanceInfo.renderPass = renderPass;
        inheritanceInfo.subpass = 0;
        inheritanceInfo.framebuffer = threadData.framebuffer;
        inheritanceInfo.occlusionQueryEnable = VK_FALSE;
        inheritanceInfo.queryFlags = 0;
        inheritanceInfo.pipelineStatistics = 0;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = &inheritanceInfo;

        if (vkBeginCommandBuffer(secondaryBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin secondary command buffer!");
        }

        pipeline->bind(secondaryBuffer);

        for (size_t r = 0; r < rangeCount; r++) {
            const uint32_t object_index = visibleIndices
                ? (*visibleIndices)[startIndex + r]
                : static_cast<uint32_t>(startIndex + r);
            if (object_index >= threadData.descriptorSets.size() || !objects[object_index].model_) {
                continue;
            }
            const auto& model = objects[object_index].model_;
            if (!model) continue;

            SimplePushConstantData push_data{};
            push_data.objectIndex = object_index;
            push_data.materialIndex = 0;

#ifdef USE_MESH_SHADING
            vkCmdPushConstants(secondaryBuffer, pipelineLayout,
                              VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);
            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout, 0, 1, &threadData.descriptorSets[object_index], 0, nullptr);
            uint32_t meshletCount = (threadData.visibleMeshletCounts && object_index < threadData.visibleMeshletCounts->size())
                ? (*threadData.visibleMeshletCounts)[object_index]
                : model->getMeshletCount();
            if (meshletCount > 0) {
                vkCmdDrawMeshTasksEXT(secondaryBuffer, meshletCount, 1, 1);
            }
#else
            vkCmdPushConstants(secondaryBuffer, pipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);
            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout, 0, 1, &threadData.descriptorSets[object_index], 0, nullptr);
            model->bind(secondaryBuffer);
            model->draw(secondaryBuffer);
#endif
        }

        if (vkEndCommandBuffer(secondaryBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to end secondary command buffer!");
        }
    }

    void RenderSystem::recordSecondaryCommandBuffers(const FrameInfo& frameinfo, const std::vector<VKObject::Object>& objects,
                                                     const std::vector<uint32_t>* visibleIndices)
    {
        const size_t countToRecord = visibleIndices ? visibleIndices->size() : objects.size();
        if (!useParallelRecording_ || threadCommandResources_.empty() || countToRecord == 0) {
            return;
        }

        if (frameinfo.framebuffer_ == VK_NULL_HANDLE) {
            throw std::runtime_error("Framebuffer is null in recordSecondaryCommandBuffers!");
        }
        if (threadCommandResources_.size() != numThreads_) {
            throw std::runtime_error("Thread command resources size mismatch!");
        }
        if (objects.size() != frameinfo.globaldescriptorsets_.size()) {
            throw std::runtime_error("Objects and descriptor sets size mismatch!");
        }

        std::vector<VkDescriptorSet> descriptorSetsCopy = frameinfo.globaldescriptorsets_;
        std::vector<PrecomputedMatrices> matricesCopy;
        if (frameinfo.precomputedMatrices_) {
            matricesCopy = *frameinfo.precomputedMatrices_;
        }
        VkFramebuffer framebuffer = frameinfo.framebuffer_;
        VkRenderPass renderPass = renderPass_;
        uint32_t frameIndex = frameinfo.frameindex_;

        size_t objectsPerThread = (countToRecord + numThreads_ - 1) / numThreads_;
        std::vector<std::thread> recordThreads;

        for (size_t threadId = 0; threadId < numThreads_ && threadId < threadCommandResources_.size(); threadId++) {
            size_t startIdx = threadId * objectsPerThread;
            size_t endIdx = std::min(startIdx + objectsPerThread, countToRecord);
            if (startIdx >= countToRecord) break;

            const std::vector<VKObject::Object>* objectsPtr = &objects;
            const std::vector<uint32_t>* visiblePtr = visibleIndices;
            VKPipeline::Pipeline* pipelinePtr = pipeline_.get();
            VkPipelineLayout pipelineLayoutVal = pipelineLayout_;
            ThreadCommandResources* threadResPtr = &threadCommandResources_[threadId];
#ifdef USE_MESH_SHADING
            PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXTVal = vkCmdDrawMeshTasksEXT_;
            std::vector<uint32_t>* visibleMeshletCountsPtr = frameinfo.visibleMeshletCounts_;
#endif
            recordThreads.emplace_back([startIdx, endIdx, frameIndex,
                                      descriptorSetsCopy, matricesCopy, framebuffer, renderPass,
                                      objectsPtr, visiblePtr, pipelinePtr, pipelineLayoutVal, threadResPtr
#ifdef USE_MESH_SHADING
                                      , vkCmdDrawMeshTasksEXTVal, visibleMeshletCountsPtr
#endif
                                      ]() {
                if (frameIndex < threadResPtr->secondaryBuffers.size()) {
                    VkCommandBuffer secondaryBuffer = threadResPtr->secondaryBuffers[frameIndex];
                    ThreadSafeFrameData threadData;
                    threadData.descriptorSets = descriptorSetsCopy;
                    threadData.precomputedMatrices = matricesCopy;
                    threadData.framebuffer = framebuffer;
                    threadData.hasPrecomputedMatrices = !matricesCopy.empty();
#ifdef USE_MESH_SHADING
                    threadData.visibleMeshletCounts = visibleMeshletCountsPtr;
#endif
                    recordSecondaryCommandsThreadSafe(secondaryBuffer, renderPass, threadData,
                                                     *objectsPtr, startIdx, endIdx, visiblePtr,
                                                     pipelinePtr, pipelineLayoutVal
#ifdef USE_MESH_SHADING
                                                     , vkCmdDrawMeshTasksEXTVal
#endif
                                                     );
                }
            });
        }

        // Wait for all threads to finish recording
        for (auto& thread : recordThreads) {
            thread.join();
        }
        
        // Diagnostic: Log successful parallel recording completion
        // (commented out for performance, uncomment for debugging)
        // std::cout << "Successfully recorded " << recordThreads.size() << " secondary command buffers in parallel" << std::endl;
    }

    void RenderSystem::executeSecondaryCommandBuffers(const FrameInfo& frameinfo, size_t objectCount)
    {
        if (!useParallelRecording_ || threadCommandResources_.empty() || objectCount == 0) {
            return;
        }

        // Calculate which buffers to execute (same logic as recording)
        std::vector<VkCommandBuffer> secondaryBuffersToExecute;
        uint32_t frameIndex = frameinfo.frameindex_;
        size_t objectsPerThread = (objectCount + numThreads_ - 1) / numThreads_;

        // Collect secondary buffers that were actually used for this frame
        for (size_t threadId = 0; threadId < numThreads_ && threadId < threadCommandResources_.size(); threadId++) {
            size_t startIdx = threadId * objectsPerThread;
            if (startIdx < objectCount) {
                auto& threadRes = threadCommandResources_[threadId];
                if (frameIndex < threadRes.secondaryBuffers.size()) {
                    secondaryBuffersToExecute.push_back(threadRes.secondaryBuffers[frameIndex]);
                }
            }
        }

        if (!secondaryBuffersToExecute.empty()) {
            vkCmdExecuteCommands(frameinfo.commandbuffer_, 
                               static_cast<uint32_t>(secondaryBuffersToExecute.size()),
                               secondaryBuffersToExecute.data());
        }
    }

}  // namespace lve