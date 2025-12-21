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

// Push constant now only contains object index (matrices are in storage buffer)
struct SimplePushConstantData 
{
    uint32_t objectIndex{0};
    uint32_t padding[3];  // Padding to align to 16 bytes (required by Vulkan)
};

// Thread-safe frame data structure for parallel command recording
struct ThreadSafeFrameData {
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<PrecomputedMatrices> precomputedMatrices;
    VkFramebuffer framebuffer;
    bool hasPrecomputedMatrices;
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
        pushConstantRange.size = sizeof(SimplePushConstantData);  // Now only 16 bytes (object index + padding)

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
        if (objects.size() == 0) {
            return;
        }

        // Write draw start timestamp if profiler is available
        if (frameinfo.profiler_ && frameinfo.profiler_->isSupported()) {
            frameinfo.profiler_->writeTimestamp(frameinfo.commandbuffer_, frameinfo.frameindex_,
                                               VKProfiler::TimestampQuery::DRAW_START,
                                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }

        // If parallel recording is enabled, secondary buffers should already be recorded
        // and we just execute them. Otherwise use sequential recording.
        if (useParallelRecording_ && threadCommandResources_.size() > 0) {
            // Execute already-recorded secondary command buffers
            executeSecondaryCommandBuffers(frameinfo, objects.size());
        } else {
            // Traditional sequential recording
            pipeline_->bind(frameinfo.commandbuffer_);

#ifdef USE_MESH_SHADING
            if (!vkCmdDrawMeshTasksEXT_) {
                throw std::runtime_error("vkCmdDrawMeshTasksEXT not initialized! Mesh shading may not be supported.");
            }

            // OPTIMIZATION: Cache last descriptor set to avoid redundant bindings
            // Many objects (buildings without textures) share the same descriptor set
            VkDescriptorSet lastBoundDescriptorSet = VK_NULL_HANDLE;
            uint32_t descriptorBindCount = 0;
            uint32_t descriptorSkipCount = 0;

            for (int object_index = 0; object_index < objects.size(); ++object_index)
            {
                if (object_index >= frameinfo.globaldescriptorsets_.size()) {
                    throw std::runtime_error("Descriptor set index out of bounds in renderObjects!");
                }

                VkDescriptorSet currentDescriptorSet = frameinfo.globaldescriptorsets_[object_index];

                // Only bind descriptor set if it changed from previous object
                if (currentDescriptorSet != lastBoundDescriptorSet) {
                    vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                        pipelineLayout_, 0, 1, &currentDescriptorSet, 0, nullptr);
                    lastBoundDescriptorSet = currentDescriptorSet;
                    descriptorBindCount++;
                } else {
                    descriptorSkipCount++;
                }

                // Push constant now only contains object index (matrices are in storage buffer)
                SimplePushConstantData push_data{};
                push_data.objectIndex = static_cast<uint32_t>(object_index);

                vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_, 
                                    VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0, sizeof(SimplePushConstantData), &push_data);

                // Mesh shaders don't use vertex/index buffers
                // Draw meshlets: one mesh task per meshlet in the model
                // OPTIMIZED: With workgroup size 32, GPU occupancy is much better
                // Parameters: commandBuffer, groupCountX, groupCountY, groupCountZ
                uint32_t meshletCount = objects[object_index].model_->getMeshletCount();
                if (meshletCount > 0) {
                    vkCmdDrawMeshTasksEXT_(frameinfo.commandbuffer_, meshletCount, 1, 1);
                } else {
                    // Fallback: draw one mesh task if no meshlets (shouldn't happen)
                    vkCmdDrawMeshTasksEXT_(frameinfo.commandbuffer_, 1, 1, 1);
                }
            }
            
            // Debug: Log descriptor binding statistics (can be removed in production)
            // std::cout << "Descriptor bindings: " << descriptorBindCount << " (skipped: " << descriptorSkipCount << ")" << std::endl;
#else
            // OPTIMIZATION: Cache descriptor bindings for classic pipeline too
            VkDescriptorSet lastBoundDescriptorSet = VK_NULL_HANDLE;
            
            for (int object_index = 0; object_index < objects.size(); ++object_index)
            {
                VkDescriptorSet currentDescriptorSet = frameinfo.globaldescriptorsets_[object_index];
                
                // Only bind descriptor set if it changed from previous object
                if (currentDescriptorSet != lastBoundDescriptorSet) {
                    vkCmdBindDescriptorSets(frameinfo.commandbuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                        pipelineLayout_, 0, 1, &currentDescriptorSet, 0, nullptr);
                    lastBoundDescriptorSet = currentDescriptorSet;
                }
                
                // Push constant now only contains object index (matrices are in storage buffer)
                SimplePushConstantData push_data{};
                push_data.objectIndex = static_cast<uint32_t>(object_index);

                vkCmdPushConstants (frameinfo.commandbuffer_, pipelineLayout_, 
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0, sizeof(SimplePushConstantData), &push_data);

                objects[object_index].model_ -> bind(frameinfo.commandbuffer_);
                objects[object_index].model_ -> draw(frameinfo.commandbuffer_);
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
                                                size_t startIndex, size_t endIndex)
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

        // Record commands for objects in this range
        for (size_t object_index = startIndex; object_index < endIndex && object_index < objects.size(); object_index++) {
            // Safety checks
            if (object_index >= frameinfo.globaldescriptorsets_.size()) {
                continue;  // Skip if out of bounds
            }
            if (!objects[object_index].model_) {
                continue;  // Skip if model is null
            }

            // Push constant now only contains object index (matrices are in storage buffer)
            SimplePushConstantData push_data{};
            push_data.objectIndex = static_cast<uint32_t>(object_index);

#ifdef USE_MESH_SHADING
            vkCmdPushConstants(secondaryBuffer, pipelineLayout_,
                              VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);

            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout_, 0, 1, &frameinfo.globaldescriptorsets_[object_index], 0, nullptr);

            uint32_t meshletCount = objects[object_index].model_->getMeshletCount();
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

        // Record commands for objects in this range
        for (size_t object_index = startIndex; object_index < endIndex && object_index < objects.size(); object_index++) {
            // Safety checks - validate all accesses before using
            if (object_index >= threadData.descriptorSets.size()) {
                continue;  // Skip if out of bounds
            }
            if (!objects[object_index].model_) {
                continue;  // Skip if model is null
            }
            
            // Additional thread-safety check: verify model pointer is still valid
            // (shared_ptr ensures object won't be deleted while in use, but we check anyway)
            const auto& model = objects[object_index].model_;
            if (!model) {
                continue;  // Double-check model is still valid
            }

            // Push constant now only contains object index (matrices are in storage buffer)
            SimplePushConstantData push_data{};
            push_data.objectIndex = static_cast<uint32_t>(object_index);

#ifdef USE_MESH_SHADING
            vkCmdPushConstants(secondaryBuffer, pipelineLayout,
                              VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);

            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout, 0, 1, &threadData.descriptorSets[object_index], 0, nullptr);

            // Thread-safe: getMeshletCount() is const and only reads data
            uint32_t meshletCount = model->getMeshletCount();
            if (meshletCount > 0) {
                vkCmdDrawMeshTasksEXT(secondaryBuffer, meshletCount, 1, 1);
            }
#else
            vkCmdPushConstants(secondaryBuffer, pipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(SimplePushConstantData), &push_data);

            vkCmdBindDescriptorSets(secondaryBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelineLayout, 0, 1, &threadData.descriptorSets[object_index], 0, nullptr);

            // Thread-safe: bind() and draw() are now const and only read from buffers
            model->bind(secondaryBuffer);
            model->draw(secondaryBuffer);
#endif
        }

        if (vkEndCommandBuffer(secondaryBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to end secondary command buffer!");
        }
    }

    void RenderSystem::recordSecondaryCommandBuffers(const FrameInfo& frameinfo, const std::vector<VKObject::Object>& objects)
    {
        if (!useParallelRecording_ || threadCommandResources_.empty() || objects.size() == 0) {
            return;
        }

        // Safety checks
        if (frameinfo.framebuffer_ == VK_NULL_HANDLE) {
            throw std::runtime_error("Framebuffer is null in recordSecondaryCommandBuffers!");
        }
        
        // Additional validation for thread safety
        if (threadCommandResources_.size() != numThreads_) {
            throw std::runtime_error("Thread command resources size mismatch!");
        }
        if (objects.size() != frameinfo.globaldescriptorsets_.size()) {
            throw std::runtime_error("Objects and descriptor sets size mismatch!");
        }

        // Create thread-safe copies of necessary data BEFORE starting threads
        // This ensures all threads have safe access to data without race conditions
        std::vector<VkDescriptorSet> descriptorSetsCopy = frameinfo.globaldescriptorsets_;
        std::vector<PrecomputedMatrices> matricesCopy;
        if (frameinfo.precomputedMatrices_) {
            matricesCopy = *frameinfo.precomputedMatrices_;
        }
        VkFramebuffer framebuffer = frameinfo.framebuffer_;
        VkRenderPass renderPass = renderPass_;
        size_t objectsSize = objects.size();

        size_t objectsPerThread = (objects.size() + numThreads_ - 1) / numThreads_;
        std::vector<std::thread> recordThreads;
        uint32_t frameIndex = frameinfo.frameindex_;

        // Record secondary command buffers in parallel
        // Use pointers to objects and copies of frameinfo data for thread safety
        for (size_t threadId = 0; threadId < numThreads_ && threadId < threadCommandResources_.size(); threadId++) {
            size_t startIdx = threadId * objectsPerThread;
            size_t endIdx = std::min(startIdx + objectsPerThread, objects.size());
            
            if (startIdx < objects.size()) {
                // Capture by value for thread safety - use copies and const pointer to objects
                // Objects vector is guaranteed to not change during recording (it's const reference)
                const std::vector<VKObject::Object>* objectsPtr = &objects;
                VKPipeline::Pipeline* pipelinePtr = pipeline_.get();
                VkPipelineLayout pipelineLayoutVal = pipelineLayout_;
#ifdef USE_MESH_SHADING
                PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXTVal = vkCmdDrawMeshTasksEXT_;
#endif
                // Capture thread resources pointer before lambda for thread safety
                ThreadCommandResources* threadResPtr = &threadCommandResources_[threadId];
                recordThreads.emplace_back([startIdx, endIdx, frameIndex, 
                                          descriptorSetsCopy, matricesCopy, framebuffer, renderPass,
                                          objectsPtr, pipelinePtr, pipelineLayoutVal, threadResPtr
#ifdef USE_MESH_SHADING
                                          , vkCmdDrawMeshTasksEXTVal
#endif
                                          ]() {
                    if (frameIndex < threadResPtr->secondaryBuffers.size()) {
                        VkCommandBuffer secondaryBuffer = threadResPtr->secondaryBuffers[frameIndex];
                        
                        // Create thread-safe frame data structure
                        ThreadSafeFrameData threadData;
                        threadData.descriptorSets = descriptorSetsCopy;
                        threadData.precomputedMatrices = matricesCopy;
                        threadData.framebuffer = framebuffer;
                        threadData.hasPrecomputedMatrices = !matricesCopy.empty();
                        
                        recordSecondaryCommandsThreadSafe(secondaryBuffer, renderPass, threadData, 
                                                         *objectsPtr, startIdx, endIdx,
                                                         pipelinePtr, pipelineLayoutVal
#ifdef USE_MESH_SHADING
                                                         , vkCmdDrawMeshTasksEXTVal
#endif
                                                         );
                    }
                });
            }
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