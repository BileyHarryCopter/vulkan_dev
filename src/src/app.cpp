#include "app.hpp"

#include "frustum.hpp"
#include "render_system.hpp"
#include "utility.hpp"
#include "gpu_monitor.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iomanip>
#include <thread>
#include <algorithm>
#include <execution>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <sstream>

namespace VKEngine
{

    static uint32_t selectLodLevelRuntime(const VKModel::Model& model,
                                          const glm::vec3& cameraPos,
                                          const glm::mat4& modelMat,
                                          float targetPixels,
                                          uint32_t previousLevel)
    {
        const auto& info = model.getLodInfo();
        if (!info.available || info.levelCount == 0) return 0;

        glm::vec3 modelMin, modelMax, worldMin, worldMax;
        model.getBoundingBox(modelMin, modelMax);
        transformAabb(modelMat, modelMin, modelMax, worldMin, worldMax);
        const glm::vec3 center = 0.5f * (worldMin + worldMax);
        const float dist = glm::length(center - cameraPos);

        // Temporal-stable hysteresis on level transitions:
        // - increase detail loss only when distance is above upper margin
        // - restore detail only when distance is below lower margin
        const float pixelScale = std::max(0.25f, targetPixels);
        const float baseDistance = 45.0f * pixelScale;
        const float upperMargin = 1.15f;
        const float lowerMargin = 0.85f;

        uint32_t level = std::min(previousLevel, info.levelCount - 1);

        auto thresholdFor = [baseDistance](uint32_t transitionLevel) {
            float t = baseDistance;
            for (uint32_t i = 0; i < transitionLevel; i++) t *= 2.0f;
            return t;
        };

        while (level + 1 < info.levelCount) {
            const float t = thresholdFor(level);
            if (dist > t * upperMargin) level++;
            else break;
        }
        while (level > 0) {
            const float t = thresholdFor(level - 1);
            if (dist < t * lowerMargin) level--;
            else break;
        }
        return level;
    }

    static uint32_t selectLodLevelForGroupRuntime(float distanceToCamera,
                                                  float groupRadiusWorld,
                                                  uint32_t levelCount,
                                                  float targetPixels,
                                                  float mixedResponse)
    {
        if (levelCount == 0) return 0;
        const float dist = std::max(0.001f, distanceToCamera);
        const float radius = std::max(0.001f, groupRadiusWorld);
        // Approximate projected radius in pixels for 1080p-equivalent vertical resolution.
        const float projectedPixels = (radius / dist) * 1080.0f;

        // Transition thresholds (0->1, 1->2, ...) in pixels.
        // The old variant used too low thresholds and stayed at L0 for large models.
        const float response = std::max(0.25f, mixedResponse);
        const float transition0 = std::max(8.0f, targetPixels * 64.0f * response);

        uint32_t level = 0;
        while (level + 1 < levelCount)
        {
            const float threshold = transition0 / static_cast<float>(1u << level);
            if (projectedPixels < threshold) level++;
            else break;
        }
        return std::min(level, levelCount - 1);
    }

    void App::run()
    {
        // LOD integration checkpoint settings (phase-gated rollout).
        lodSettings_.phase = VKLod::parsePhaseFromEnv();
        if (const char* rawEnable = std::getenv("VK_LOD_ENABLE")) {
            lodSettings_.enabled = (std::atoi(rawEnable) != 0);
        }
        if (const char* rawForce = std::getenv("VK_LOD_FORCE_LEVEL")) {
            lodSettings_.forcedLevel = std::atoi(rawForce);
        }
        if (const char* rawTarget = std::getenv("VK_LOD_TARGET_PIXELS")) {
            lodSettings_.targetPixels = std::atof(rawTarget);
        }
        if (const char* rawDbg = std::getenv("VK_LOD_DEBUG")) {
            lodSettings_.debugLogs = (std::atoi(rawDbg) != 0);
        }
        if (const char* rawDbgInterval = std::getenv("VK_LOD_DEBUG_INTERVAL")) {
            lodSettings_.debugIntervalSeconds = std::max(0.1f, static_cast<float>(std::atof(rawDbgInterval)));
        }
        if (const char* rawDbgMaxObjs = std::getenv("VK_LOD_DEBUG_MAX_OBJECTS")) {
            lodSettings_.debugMaxObjects = std::max<uint32_t>(1u, static_cast<uint32_t>(std::atoi(rawDbgMaxObjs)));
        }
        if (const char* rawMixedResp = std::getenv("VK_LOD_MIXED_RESPONSE")) {
            lodSettings_.mixedResponse = std::max(0.25f, static_cast<float>(std::atof(rawMixedResp)));
        }
        std::cout << "LOD checkpoint phase: " << VKLod::phaseName(lodSettings_.phase)
                  << " (enabled=" << (lodSettings_.enabled ? "true" : "false")
                  << ", forcedLevel=" << lodSettings_.forcedLevel
                  << ", targetPixels=" << lodSettings_.targetPixels
                  << ", mixedResponse=" << lodSettings_.mixedResponse
                  << ", debugLogs=" << (lodSettings_.debugLogs ? "true" : "false")
                  << ", debugInterval=" << lodSettings_.debugIntervalSeconds << "s"
                  << ")" << std::endl;

        //  creating uniform buffers for global data
        std::vector<std::unique_ptr<VKBuffmanager::Buffmanager>> ubobuffs (VKSwapchain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < ubobuffs.size(); ++i)
        {
            ubobuffs[i] = std::make_unique<VKBuffmanager::Buffmanager> (device_, sizeof(GlobalUbo), VKSwapchain::MAX_FRAMES_IN_FLIGHT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, (uint16_t) device_.get_properties().limits.minUniformBufferOffsetAlignment);
            ubobuffs[i]->map();
        }

        // Create storage buffer for object matrices (shared across all objects)
        using ObjectMatrices = VKRenderSystem::ObjectMatrices;
        objectMatricesBuffers_.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < objectMatricesBuffers_.size(); ++i)
        {
            // Calculate buffer size: one ObjectMatrices per object
            VkDeviceSize bufferSize = sizeof(ObjectMatrices) * objects_.size();
            // Align to minimum storage buffer offset alignment
            VkDeviceSize minAlignment = device_.get_properties().limits.minStorageBufferOffsetAlignment;
            if (minAlignment > 0) {
                bufferSize = (bufferSize + minAlignment - 1) & ~(minAlignment - 1);
            }
            
            objectMatricesBuffers_[i] = std::make_unique<VKBuffmanager::Buffmanager>(
                device_, 
                sizeof(ObjectMatrices), 
                static_cast<uint32_t>(objects_.size()),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                static_cast<uint16_t>(minAlignment)
            );
            objectMatricesBuffers_[i]->map();
        }

        // Collect all materials from all models
        allMaterials_.clear();
        for (const auto& obj : objects_)
        {
            if (obj.model_)
            {
                const auto& modelMaterials = obj.model_->getMaterials();
                allMaterials_.insert(allMaterials_.end(), modelMaterials.begin(), modelMaterials.end());
            }
        }
        
        // If no materials found, create a default one
        if (allMaterials_.empty())
        {
            VKModel::Material defaultMaterial;
            allMaterials_.push_back(defaultMaterial);
        }

        // Create storage buffer for materials (shared across all objects)
        using Material = VKModel::Material;
        materialsBuffers_.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < materialsBuffers_.size(); ++i)
        {
            // Calculate buffer size: one Material per material
            VkDeviceSize bufferSize = sizeof(Material) * allMaterials_.size();
            // Align to minimum storage buffer offset alignment
            VkDeviceSize minAlignment = device_.get_properties().limits.minStorageBufferOffsetAlignment;
            if (minAlignment > 0) {
                bufferSize = (bufferSize + minAlignment - 1) & ~(minAlignment - 1);
            }
            
            materialsBuffers_[i] = std::make_unique<VKBuffmanager::Buffmanager>(
                device_, 
                sizeof(Material), 
                static_cast<uint32_t>(allMaterials_.size()),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                static_cast<uint16_t>(minAlignment)
            );
            materialsBuffers_[i]->map();
            
            // Write initial material data
            materialsBuffers_[i]->writeToBuffer(allMaterials_.data(), sizeof(Material) * allMaterials_.size());
        }

#ifdef USE_MESH_SHADING
        // Per-frame, per-object buffers for visible meshlet indices (layout: count at [0], then indices). For meshlet culling.
        visibleMeshletCounts_.resize(objects_.size(), 0);
        runtimeLodLevels_.assign(objects_.size(), 0);
        visibleMeshletBuffers_.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);
        for (int f = 0; f < VKSwapchain::MAX_FRAMES_IN_FLIGHT; f++) {
            visibleMeshletBuffers_[f].resize(objects_.size());
            for (size_t o = 0; o < objects_.size(); o++) {
                uint32_t n = objects_[o].model_ ? objects_[o].model_->getMeshletCount() : 0;
                uint32_t count = (n > 0) ? (1 + n) : 1;  // at least [count] slot
                visibleMeshletBuffers_[f][o] = std::make_unique<VKBuffmanager::Buffmanager>(
                    device_, sizeof(uint32_t), count,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
                visibleMeshletBuffers_[f][o]->map();
            }
        }
#endif

        //  creating layout for GLOBAL set and it respectively
#ifdef USE_MESH_SHADING
        // For mesh shading: UBO, textures, storage buffers for meshlets, visible meshlet indices, object matrices, and materials
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet structures
            .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet vertices
            .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet triangles
            .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // vertex data
            .addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)  // object matrices
            .addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)  // materials
            .addBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // visible meshlet indices (count at [0], then indices)
            .build();
#else
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
            .addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)  // object matrices
            .addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)  // materials
            .build();
#endif
        std::vector<VkDescriptorSet> allDescriptorsets(VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size()); 
        int descriptorSetIndex = 0;
        for (int frame = 0; frame < VKSwapchain::MAX_FRAMES_IN_FLIGHT; frame++) 
        {
            auto bufferInfo = ubobuffs[frame]->descriptorInfo();
            // Object matrices storage buffer (shared across all objects in this frame)
            auto objectMatricesBufferInfo = objectMatricesBuffers_[frame]->descriptorInfo();
            // Materials storage buffer (shared across all objects in this frame)
            auto materialsBufferInfo = materialsBuffers_[frame]->descriptorInfo();

            for (auto& obj : objects_)
            {
#ifdef USE_MESH_SHADING
                // For mesh shading: UBO, textures, storage buffers for meshlets, and object matrices
                if (!obj.model_) {
                    throw std::runtime_error("Model is null for object!");
                }
                if (descriptorSetIndex >= allDescriptorsets.size()) {
                    throw std::runtime_error("Descriptor set index out of bounds!");
                }
                
                // UBO (binding 0)
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = obj.model_->getimgview();
                imageInfo.sampler = obj.model_->getsampler();
                
                // Storage buffers for meshlets
                VkDescriptorBufferInfo meshletBufferInfo = obj.model_->getMeshletBuffer() != VK_NULL_HANDLE ? 
                    VkDescriptorBufferInfo{obj.model_->getMeshletBuffer(), 0, VK_WHOLE_SIZE} : 
                    VkDescriptorBufferInfo{VK_NULL_HANDLE, 0, 0};
                VkDescriptorBufferInfo meshletVerticesBufferInfo = obj.model_->getMeshletVerticesBuffer() != VK_NULL_HANDLE ?
                    VkDescriptorBufferInfo{obj.model_->getMeshletVerticesBuffer(), 0, VK_WHOLE_SIZE} :
                    VkDescriptorBufferInfo{VK_NULL_HANDLE, 0, 0};
                VkDescriptorBufferInfo meshletTrianglesBufferInfo = obj.model_->getMeshletTrianglesBuffer() != VK_NULL_HANDLE ?
                    VkDescriptorBufferInfo{obj.model_->getMeshletTrianglesBuffer(), 0, VK_WHOLE_SIZE} :
                    VkDescriptorBufferInfo{VK_NULL_HANDLE, 0, 0};
                VkDescriptorBufferInfo vertexDataBufferInfo = obj.model_->getVertexDataBuffer() != VK_NULL_HANDLE ?
                    VkDescriptorBufferInfo{obj.model_->getVertexDataBuffer(), 0, VK_WHOLE_SIZE} :
                    VkDescriptorBufferInfo{VK_NULL_HANDLE, 0, 0};
                size_t objIdx = static_cast<size_t>(descriptorSetIndex - frame * objects_.size());
                VkDescriptorBufferInfo visibleMeshletBufferInfo = visibleMeshletBuffers_[frame][objIdx]
                    ? visibleMeshletBuffers_[frame][objIdx]->descriptorInfo()
                    : VkDescriptorBufferInfo{VK_NULL_HANDLE, 0, 0};

                VKDescriptors::DescriptorWriter writer(*setlayout, *globalPool);
                writer.writeBuffer(0, &bufferInfo)  // UBO
                      .writeImage(1, &imageInfo)    // Texture
                      .writeBuffer(2, &meshletBufferInfo)  // Meshlet structures
                      .writeBuffer(3, &meshletVerticesBufferInfo)  // Meshlet vertex indices
                      .writeBuffer(4, &meshletTrianglesBufferInfo)  // Meshlet triangle indices
                      .writeBuffer(5, &vertexDataBufferInfo)  // Vertex data
                      .writeBuffer(6, &objectMatricesBufferInfo)  // Object matrices (shared)
                      .writeBuffer(7, &materialsBufferInfo)  // Materials (shared)
                      .writeBuffer(8, &visibleMeshletBufferInfo);  // Visible meshlet indices
                writer.build(allDescriptorsets[descriptorSetIndex]);
#else
                if (!obj.model_) {
                    throw std::runtime_error("Model is null for object!");
                }
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = obj.model_->getimgview();
                imageInfo.sampler = obj.model_->getsampler();

                if (descriptorSetIndex >= allDescriptorsets.size()) {
                    throw std::runtime_error("Descriptor set index out of bounds!");
                }
                VKDescriptors::DescriptorWriter writer(*setlayout, *globalPool);
                writer.writeBuffer(0, &bufferInfo)  // UBO
                      .writeImage(1, &imageInfo)    // Texture
                      .writeBuffer(6, &objectMatricesBufferInfo)   // Object matrices (shared)
                      .writeBuffer(7, &materialsBufferInfo);        // Materials (shared)
                writer.build(allDescriptorsets[descriptorSetIndex]);
#endif
                descriptorSetIndex++;  // Increment the index explicitly
            }
        }

        auto descriptorSetLayouts = std::vector<VkDescriptorSetLayout> {setlayout->getDescriptorSetLayout()};
        VKRenderSystem::RenderSystem renderSystem {device_, renderer_.getSwapChainRenderPass(), descriptorSetLayouts};
        

        renderSystem.enableParallelRecording(false);


        VKCamera::Camera camera{};

        auto viewerObject =    VKObject::Object::createObject();
        // Start farther from the model on horizontal axis to observe coarser mixed LOD levels earlier.
        viewerObject.transform3D_.translation = {0.0f, 0.0f, -350.0f};
        viewerObject.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
        VKKeyboardController::KeyboardController cameraController{};

        // Debug: Log initial object information
        std::cout << "Loaded " << objects_.size() << " objects" << std::endl;
        if (objects_.size() > 0) {
            std::cout << "First object position: (" 
                      << objects_[0].transform3D_.translation.x << ", "
                      << objects_[0].transform3D_.translation.y << ", "
                      << objects_[0].transform3D_.translation.z << ")" << std::endl;
            std::cout << "First object scale: (" 
                      << objects_[0].transform3D_.scale.x << ", "
                      << objects_[0].transform3D_.scale.y << ", "
                      << objects_[0].transform3D_.scale.z << ")" << std::endl;
        }

        auto currentTime = std::chrono::high_resolution_clock::now();
        auto frameStartTime = currentTime;
        auto lastStatsTime = currentTime;
        auto lastLodDebugTime = currentTime;
        constexpr double STATS_INTERVAL_SECONDS = 2.0;
        int frameCount = 0;
        
        double realFrameTimeMs = 0.0;
        double realFps = 0.0;
        
        double matrixPrepTimeMs = 0.0;
        double descriptorPrepTimeMs = 0.0;
        double commandRecordTimeMs = 0.0;
        
        std::vector<VKRenderSystem::PrecomputedMatrices> precomputedMatrices(objects_.size());
        
        VKGpuMonitor::GpuMonitor gpuMonitor;
        if (gpuMonitor.isAvailable()) {
            std::cout << "GPU Monitor initialized: " << gpuMonitor.getGpuName() << std::endl;
        } else {
            std::cout << "GPU Monitor not available (nvidia-smi not found)" << std::endl;
        }
        
        // Get number of available CPU threads
        const size_t numThreads = std::max(1u, std::thread::hardware_concurrency());
        std::cout << "Available CPU threads: " << numThreads << std::endl;

        try {
            while(!window_.shouldClose())
            {
                try {
                    glfwPollEvents();

                    auto newTime = std::chrono::high_resolution_clock::now();
                    float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
                    currentTime = newTime;

                    cameraController.moveInPlaneXZ(window_.get(), frameTime, viewerObject);
                    camera.setViewYXZ(viewerObject.transform3D_.translation, viewerObject.transform3D_.rotation);

                    float aspect = renderer_.getAspectRatio();
                    camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 10000.f);

                    if (frameCount % 60 == 0) {
                        std::cout << "Camera position: (" 
                                  << viewerObject.transform3D_.translation.x << ", "
                                  << viewerObject.transform3D_.translation.y << ", "
                                  << viewerObject.transform3D_.translation.z << ")" << std::endl;
                        std::cout << "Camera rotation: (" 
                                  << viewerObject.transform3D_.rotation.x << ", "
                                  << viewerObject.transform3D_.rotation.y << ", "
                                  << viewerObject.transform3D_.rotation.z << ")" << std::endl;
                    }
                    frameCount++;
                    
                    auto frameEndTime = std::chrono::high_resolution_clock::now();
                    if (frameCount > 1) {
                        realFrameTimeMs = std::chrono::duration<double, std::milli>(frameEndTime - frameStartTime).count();
                        realFps = realFrameTimeMs > 0.0 ? 1000.0 / realFrameTimeMs : 0.0;
                    }
                    
                    frameStartTime = frameEndTime;

                    if (auto commandBuffer = renderer_.beginFrame())
                    {
                        int frameindex = renderer_.getframeindex();
                        
                        auto matrixPrepStart = std::chrono::high_resolution_clock::now();
                        
                        std::vector<std::thread> matrixThreads;
                        
                        if (segmentRanges_.empty()) {
                            // Fallback: if no segments, use object-based parallelization
                            const size_t objectsPerThread = (objects_.size() + numThreads - 1) / numThreads;
                            
                            for (size_t threadId = 0; threadId < numThreads; threadId++) {
                                size_t startIdx = threadId * objectsPerThread;
                                size_t endIdx = std::min(startIdx + objectsPerThread, objects_.size());
                                
                                if (startIdx >= objects_.size()) break;
                                
                                matrixThreads.emplace_back([&, startIdx, endIdx]() {
                                    for (size_t i = startIdx; i < endIdx; i++) {
                                        // For static objects: to cache matricies
                                        if (objects_[i].transform3D_.isStatic) {
                                            precomputedMatrices[i].modelMatrix = objects_[i].transform3D_.getCachedModelMatrix();
                                            precomputedMatrices[i].normalMatrix = objects_[i].transform3D_.getCachedNormalMatrix();
                                        } else {
                                            precomputedMatrices[i].modelMatrix = objects_[i].transform3D_.mat4();
                                            precomputedMatrices[i].normalMatrix = objects_[i].transform3D_.normalMatrix();
                                        }
                                    }
                                });
                            }
                        } else {
                            // Segment-based parallelization: one thread per segment
                            for (const auto& segRange : segmentRanges_) {
                                matrixThreads.emplace_back([&, segRange]() {
                                    for (size_t i = segRange.startIndex; i < segRange.endIndex; i++) {
                                        if (i < precomputedMatrices.size()) {
                                            // For static objects: to cache matricies
                                            if (objects_[i].transform3D_.isStatic) {
                                                precomputedMatrices[i].modelMatrix = objects_[i].transform3D_.getCachedModelMatrix();
                                                precomputedMatrices[i].normalMatrix = objects_[i].transform3D_.getCachedNormalMatrix();
                                            } else {
                                                precomputedMatrices[i].modelMatrix = objects_[i].transform3D_.mat4();
                                                precomputedMatrices[i].normalMatrix = objects_[i].transform3D_.normalMatrix();
                                            }
                                        }
                                    }
                                });
                            }
                        }
                        
                        // Wait for all threads to complete
                        for (auto& thread : matrixThreads) {
                            thread.join();
                        }
                        
                        auto matrixPrepEnd = std::chrono::high_resolution_clock::now();
                        matrixPrepTimeMs = std::chrono::duration<double, std::milli>(matrixPrepEnd - matrixPrepStart).count();

                        // Prepare descriptor sets (can also be parallelized later)
                        auto descriptorPrepStart = std::chrono::high_resolution_clock::now();
                        std::vector<VkDescriptorSet> descriptorsets {};
                        for (int i = 0, len = objects_.size(); i < objects_.size(); i++) {
                            int descriptorIndex = len * frameindex + i;
                            if (descriptorIndex >= allDescriptorsets.size()) {
                                throw std::runtime_error("Descriptor set index out of bounds when building frame descriptors!");
                            }
                            descriptorsets.push_back(allDescriptorsets[descriptorIndex]);
                        }
                        auto descriptorPrepEnd = std::chrono::high_resolution_clock::now();
                        descriptorPrepTimeMs = std::chrono::duration<double, std::milli>(descriptorPrepEnd - descriptorPrepStart).count();
                        

                        VKRenderSystem::FrameInfo frameinfo {
                            frameindex, 
                            frameTime, 
                            commandBuffer, 
                            camera, 
                            descriptorsets,
                            &precomputedMatrices,
                            renderer_.getProfiler(),
                            renderer_.getSwapChainFramebuffer()
                        };

                        //  update Ubo
                        GlobalUbo ubo{};
                        ubo.projectionViewMatrix = camera.getProjection() * camera.getView();

                        // Debug: Log projection/view matrix info every 60 frames
                        if (frameCount % 60 == 1) {
                            auto proj = camera.getProjection();
                            auto view = camera.getView();
                            std::cout << "Projection matrix (first row): (" 
                                      << proj[0][0] << ", " << proj[0][1] << ", " << proj[0][2] << ", " << proj[0][3] << ")" << std::endl;
                            std::cout << "View matrix (first row): (" 
                                      << view[0][0] << ", " << view[0][1] << ", " << view[0][2] << ", " << view[0][3] << ")" << std::endl;
                        }

                        ubobuffs[frameindex]->writeToBuffer(&ubo);
                        
                        // Update object matrices storage buffer (one write for all objects)
                        using ObjectMatrices = VKRenderSystem::ObjectMatrices;
                        std::vector<ObjectMatrices> objectMatricesData(objects_.size());
                        for (size_t i = 0; i < objects_.size(); i++) {
                            if (precomputedMatrices.size() > i) {
                                objectMatricesData[i].modelMatrix = precomputedMatrices[i].modelMatrix;
                                objectMatricesData[i].normalMatrix = precomputedMatrices[i].normalMatrix;
                            } else {
                                // Fallback: compute on-the-fly if not precomputed
                                objectMatricesData[i].modelMatrix = objects_[i].transform3D_.mat4();
                                objectMatricesData[i].normalMatrix = objects_[i].transform3D_.normalMatrix();
                            }
                        }
                        objectMatricesBuffers_[frameindex]->writeToBuffer(objectMatricesData.data(), sizeof(ObjectMatrices) * objectMatricesData.size());
                        
                        // Batch flush buffers for better performance
                        std::vector<VKBuffmanager::Buffmanager*> buffersToFlush = {
                            ubobuffs[frameindex].get(),
                            objectMatricesBuffers_[frameindex].get(),
                            materialsBuffers_[frameindex].get()
                        };
                        VKBuffmanager::Buffmanager::flushMultiple(device_, buffersToFlush);

                        std::vector<uint64_t> lodDebugGroupsPerLevelFrame;
                        std::vector<uint64_t> lodDebugMeshletsPerLevelFrame;
                        uint32_t lodDebugObjectsWithHierarchyFrame = 0;
                        uint32_t lodDebugObjectsMixedFrame = 0;
                        float lodDebugMixedProjectedMin = std::numeric_limits<float>::max();
                        float lodDebugMixedProjectedMax = 0.0f;
                        std::vector<std::string> lodDebugObjectSamples;

                        // Frustum culling: build list of visible object indices
                        const glm::mat4 viewProj = camera.getProjection() * camera.getView();
                        std::vector<uint32_t> visibleIndices;
                        visibleIndices.reserve(objects_.size());
                        for (size_t i = 0; i < objects_.size(); i++) {
                            if (!objects_[i].model_) continue;
                            glm::vec3 aabbMin, aabbMax;
                            objects_[i].model_->getBoundingBox(aabbMin, aabbMax);
                            const glm::mat4 modelMat = (i < precomputedMatrices.size())
                                ? precomputedMatrices[i].modelMatrix
                                : objects_[i].transform3D_.mat4();
                            glm::vec3 wMin, wMax;
                            transformAabb(modelMat, aabbMin, aabbMax, wMin, wMax);
                            if (intersectFrustum(viewProj, wMin, wMax)) {
                                visibleIndices.push_back(static_cast<uint32_t>(i));
                            }
                        }
                        // Safety: if culling produced no visible objects but we have objects, render all
                        const std::vector<uint32_t>* visiblePtr = visibleIndices.empty() ? nullptr : &visibleIndices;

#ifdef USE_MESH_SHADING
                        // Meshlet culling + LOD checkpoint stages:
                        // Phase 0-2: regular meshlet frustum culling.
                        // Phase 3+: optional discrete LOD thinning by distance (start of runtime LOD integration).
                        const size_t numToDraw = visiblePtr ? visiblePtr->size() : objects_.size();
                        const glm::vec3 cameraPos = viewerObject.transform3D_.translation;
                        for (size_t k = 0; k < numToDraw; k++) {
                            const size_t objIdx = visiblePtr ? (*visiblePtr)[k] : k;
                            if (!objects_[objIdx].model_) { visibleMeshletCounts_[objIdx] = 0; continue; }
                            VKModel::Model* model = objects_[objIdx].model_.get();
                            const uint32_t meshletCount = model->getMeshletCount();
                            if (meshletCount == 0) { visibleMeshletCounts_[objIdx] = 0; continue; }
                            const glm::mat4 modelMat = (objIdx < precomputedMatrices.size())
                                ? precomputedMatrices[objIdx].modelMatrix
                                : objects_[objIdx].transform3D_.mat4();
                            std::vector<uint32_t> visibleMeshlets;
                            visibleMeshlets.reserve(meshletCount);

                            // Hierarchical CPU traversal path (group -> meshlets) for LOD cache models.
                            const bool useHierarchyTraversal =
                                lodSettings_.enabled &&
                                static_cast<uint32_t>(lodSettings_.phase) >= static_cast<uint32_t>(VKLod::IntegrationPhase::HierarchicalCpuLod) &&
                                model->hasLodHierarchy();
                            std::vector<uint32_t> lodDebugObjectGroupsPerLevel;
                            std::vector<uint32_t> lodDebugObjectMeshletsPerLevel;

                            if (useHierarchyTraversal) {
                                const auto& levels = model->getLodLevels();
                                const auto& nodes = model->getLodNodes();
                                const auto& groups = model->getLodGroups();
                                const auto& groupMeshlets = model->getLodGroupMeshletIndices();
                                if (lodSettings_.debugLogs) {
                                    lodDebugObjectGroupsPerLevel.assign(levels.size(), 0u);
                                    lodDebugObjectMeshletsPerLevel.assign(levels.size(), 0u);
                                    lodDebugObjectsWithHierarchyFrame++;
                                }
                                if (levels.size() <= 1 || lodSettings_.forcedLevel >= 0) {
                                    // Single-level hierarchy traversal (forced/static level).
                                    uint32_t rootNode = model->getLodRootNode();
                                    uint32_t selectedLevel = 0;
                                    if (lodSettings_.forcedLevel >= 0 && static_cast<size_t>(lodSettings_.forcedLevel) < levels.size()) {
                                        selectedLevel = static_cast<uint32_t>(lodSettings_.forcedLevel);
                                        rootNode = levels[static_cast<size_t>(lodSettings_.forcedLevel)].rootNode;
                                    }
                                    std::vector<uint32_t> stack;
                                    stack.push_back(rootNode);
                                    while (!stack.empty()) {
                                        const uint32_t nodeIdx = stack.back();
                                        stack.pop_back();
                                        if (nodeIdx >= nodes.size()) continue;
                                        const auto& node = nodes[nodeIdx];

                                        const glm::vec3 c(node.boundingSphere[0], node.boundingSphere[1], node.boundingSphere[2]);
                                        const float r = node.boundingSphere[3];
                                        glm::vec3 wMin, wMax;
                                        transformAabb(modelMat, c - glm::vec3(r), c + glm::vec3(r), wMin, wMax);
                                        if (!intersectFrustum(viewProj, wMin, wMax)) continue;

                                        if (node.groupIndex != UINT32_MAX) {
                                            if (node.groupIndex < groups.size()) {
                                                const auto& g = groups[node.groupIndex];
                                                const uint32_t end = g.meshletOffset + g.meshletCount;
                                                if (end <= groupMeshlets.size()) {
                                                    visibleMeshlets.insert(visibleMeshlets.end(),
                                                        groupMeshlets.begin() + g.meshletOffset,
                                                        groupMeshlets.begin() + end);
                                                    if (lodSettings_.debugLogs && selectedLevel < lodDebugObjectGroupsPerLevel.size()) {
                                                        lodDebugObjectGroupsPerLevel[selectedLevel] += 1u;
                                                        lodDebugObjectMeshletsPerLevel[selectedLevel] += g.meshletCount;
                                                    }
                                                }
                                            }
                                        } else {
                                            const uint32_t begin = node.firstChild;
                                            const uint32_t end = begin + node.childCount;
                                            for (uint32_t ci = begin; ci < end; ci++) {
                                                if (ci < nodes.size()) stack.push_back(ci);
                                            }
                                        }
                                    }
                                } else {
                                    // Mixed-LOD traversal:
                                    // Use finest-level groups as spatial partitions and choose target level per partition.
                                    const auto& finest = levels.front();
                                    if (lodSettings_.debugLogs) lodDebugObjectsMixedFrame++;
                                    if (finest.groupOffset + finest.groupCount <= groups.size()) {
                                        std::vector<std::vector<uint8_t>> selected(levels.size());
                                        for (size_t li = 0; li < levels.size(); li++) {
                                            selected[li].assign(levels[li].groupCount, 0u);
                                        }

                                        // Approximate world-space max scale for sphere radius.
                                        const float sx = glm::length(glm::vec3(modelMat[0]));
                                        const float sy = glm::length(glm::vec3(modelMat[1]));
                                        const float sz = glm::length(glm::vec3(modelMat[2]));
                                        const float maxScale = std::max(sx, std::max(sy, sz));

                                        for (uint32_t i = 0; i < finest.groupCount; i++) {
                                            const uint32_t fineGroupGlobal = finest.groupOffset + i;
                                            const auto& fg = groups[fineGroupGlobal];

                                            const glm::vec3 cLocal(fg.boundingSphere[0], fg.boundingSphere[1], fg.boundingSphere[2]);
                                            const float rWorld = fg.boundingSphere[3] * maxScale;
                                            const glm::vec3 cWorld = glm::vec3(modelMat * glm::vec4(cLocal, 1.0f));

                                            glm::vec3 wMin, wMax;
                                            wMin = cWorld - glm::vec3(rWorld);
                                            wMax = cWorld + glm::vec3(rWorld);
                                            if (!intersectFrustum(viewProj, wMin, wMax)) continue;

                                            const float dist = glm::length(cWorld - cameraPos);
                                            const float projectedPixels = (std::max(0.001f, rWorld) / std::max(0.001f, dist)) * 1080.0f;
                                            if (lodSettings_.debugLogs) {
                                                lodDebugMixedProjectedMin = std::min(lodDebugMixedProjectedMin, projectedPixels);
                                                lodDebugMixedProjectedMax = std::max(lodDebugMixedProjectedMax, projectedPixels);
                                            }
                                            uint32_t levelIdx = selectLodLevelForGroupRuntime(
                                                dist, rWorld, static_cast<uint32_t>(levels.size()),
                                                lodSettings_.targetPixels, lodSettings_.mixedResponse);

                                            if (levelIdx >= levels.size()) levelIdx = static_cast<uint32_t>(levels.size() - 1);
                                            const auto& targetLevel = levels[levelIdx];
                                            if (targetLevel.groupCount == 0) continue;
                                            const uint32_t ord = std::min(
                                                static_cast<uint32_t>((static_cast<uint64_t>(i) * targetLevel.groupCount) / std::max<uint32_t>(1u, finest.groupCount)),
                                                targetLevel.groupCount - 1);
                                            selected[levelIdx][ord] = 1u;
                                        }

                                        for (size_t li = 0; li < levels.size(); li++) {
                                            const auto& lvl = levels[li];
                                            for (uint32_t ord = 0; ord < lvl.groupCount; ord++) {
                                                if (!selected[li][ord]) continue;
                                                const uint32_t gi = lvl.groupOffset + ord;
                                                if (gi >= groups.size()) continue;
                                                const auto& g = groups[gi];
                                                const uint32_t end = g.meshletOffset + g.meshletCount;
                                                if (end <= groupMeshlets.size()) {
                                                    visibleMeshlets.insert(visibleMeshlets.end(),
                                                        groupMeshlets.begin() + g.meshletOffset,
                                                        groupMeshlets.begin() + end);
                                                    if (lodSettings_.debugLogs && li < lodDebugObjectGroupsPerLevel.size()) {
                                                        lodDebugObjectGroupsPerLevel[li] += 1u;
                                                        lodDebugObjectMeshletsPerLevel[li] += g.meshletCount;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                // Baseline meshlet frustum culling path.
                                for (uint32_t m = 0; m < meshletCount; m++) {
                                    glm::vec3 aabbMin, aabbMax;
                                    model->getMeshletAABB(m, aabbMin, aabbMax);
                                    glm::vec3 wMin, wMax;
                                    transformAabb(modelMat, aabbMin, aabbMax, wMin, wMax);
                                    if (intersectFrustum(viewProj, wMin, wMax))
                                        visibleMeshlets.push_back(m);
                                }
                            }

                            if (visibleMeshlets.size() > 1) {
                                std::sort(visibleMeshlets.begin(), visibleMeshlets.end());
                                visibleMeshlets.erase(std::unique(visibleMeshlets.begin(), visibleMeshlets.end()), visibleMeshlets.end());
                            }

                            if (lodSettings_.debugLogs && useHierarchyTraversal && !lodDebugObjectGroupsPerLevel.empty()) {
                                if (lodDebugGroupsPerLevelFrame.size() < lodDebugObjectGroupsPerLevel.size()) {
                                    lodDebugGroupsPerLevelFrame.resize(lodDebugObjectGroupsPerLevel.size(), 0u);
                                    lodDebugMeshletsPerLevelFrame.resize(lodDebugObjectMeshletsPerLevel.size(), 0u);
                                }
                                uint32_t totalGroups = 0;
                                for (size_t li = 0; li < lodDebugObjectGroupsPerLevel.size(); li++) {
                                    lodDebugGroupsPerLevelFrame[li] += lodDebugObjectGroupsPerLevel[li];
                                    lodDebugMeshletsPerLevelFrame[li] += lodDebugObjectMeshletsPerLevel[li];
                                    totalGroups += lodDebugObjectGroupsPerLevel[li];
                                }
                                if (totalGroups > 0 && lodDebugObjectSamples.size() < lodSettings_.debugMaxObjects) {
                                    std::ostringstream oss;
                                    oss << "obj#" << objIdx << " groups=[";
                                    for (size_t li = 0; li < lodDebugObjectGroupsPerLevel.size(); li++) {
                                        if (li > 0) oss << ", ";
                                        oss << "L" << li << ":" << lodDebugObjectGroupsPerLevel[li];
                                    }
                                    oss << "] meshletsVisible=" << visibleMeshlets.size();
                                    lodDebugObjectSamples.push_back(oss.str());
                                }
                            }

                            if (lodSettings_.enabled &&
                                static_cast<uint32_t>(lodSettings_.phase) >= static_cast<uint32_t>(VKLod::IntegrationPhase::RuntimeDiscreteLod))
                            {
                                uint32_t stride = 1;
                                if (model->hasLod()) 
                                {
                                    // IMPORTANT:
                                    // If hierarchical traversal is active, visible meshlets already come from
                                    // group->meshlet mapping and must not be additionally thinned by stride.
                                    if (!useHierarchyTraversal && lodSettings_.forcedLevel < 0) 
                                    {
                                        const uint32_t prevLevel = (objIdx < runtimeLodLevels_.size()) ? runtimeLodLevels_[objIdx] : 0;
                                        const uint32_t level = selectLodLevelRuntime(*model, cameraPos, modelMat, lodSettings_.targetPixels, prevLevel);
                                        if (objIdx < runtimeLodLevels_.size()) runtimeLodLevels_[objIdx] = level;
                                        stride = (level >= 31u) ? 0x7fffffffu : (1u << level);
                                    }
                                } else {
                                    // Fallback path for non-LOD models.
                                    glm::vec3 modelMin, modelMax, worldMin, worldMax;
                                    model->getBoundingBox(modelMin, modelMax);
                                    transformAabb(modelMat, modelMin, modelMax, worldMin, worldMax);
                                    const glm::vec3 center = 0.5f * (worldMin + worldMax);
                                    const float dist = glm::length(center - cameraPos);
                                    if (dist > 300.f) stride = 8;
                                    else if (dist > 180.f) stride = 4;
                                    else if (dist > 90.f) stride = 2;
                                }

                                if (stride > 1 && !visibleMeshlets.empty()) {
                                    std::vector<uint32_t> thinned;
                                    thinned.reserve((visibleMeshlets.size() + stride - 1) / stride);
                                    // Deterministic subset based on meshlet id, stable under camera motion.
                                    for (uint32_t meshletId : visibleMeshlets) {
                                        if ((meshletId % stride) == 0u) {
                                            thinned.push_back(meshletId);
                                        }
                                    }
                                    if (thinned.empty()) thinned.push_back(visibleMeshlets[0]);
                                    visibleMeshlets.swap(thinned);
                                }
                            }

                            visibleMeshletCounts_[objIdx] = static_cast<uint32_t>(visibleMeshlets.size());
                            VKBuffmanager::Buffmanager* buf = visibleMeshletBuffers_[frameindex][objIdx].get();
                            if (!buf) continue;
                            buf->writeToBuffer(&visibleMeshletCounts_[objIdx], sizeof(uint32_t));
                            if (!visibleMeshlets.empty())
                                buf->writeToBuffer(visibleMeshlets.data(), sizeof(uint32_t) * visibleMeshlets.size(), sizeof(uint32_t));
                        }
                        frameinfo.visibleMeshletCounts_ = &visibleMeshletCounts_;
#endif

                        //  renderer - measure command recording time
                        auto commandRecordStart = std::chrono::high_resolution_clock::now();

                        if (renderSystem.isParallelRecordingEnabled()) {
                            renderSystem.recordSecondaryCommandBuffers(frameinfo, objects_, visiblePtr);
                        }

                        renderer_.beginSwapchainRenderpass(commandBuffer, renderSystem.isParallelRecordingEnabled());

                        renderSystem.renderObjects(frameinfo, objects_, visiblePtr);
                        
                        renderer_.endSwapchainRenderpass(commandBuffer);
                        auto commandRecordEnd = std::chrono::high_resolution_clock::now();
                        commandRecordTimeMs = std::chrono::duration<double, std::milli>(commandRecordEnd - commandRecordStart).count();
                        
                        renderer_.endFrame();
                        
                        // Periodically read and display performance statistics
                        auto now = std::chrono::high_resolution_clock::now();
                        double timeSinceLastStats = std::chrono::duration<double>(now - lastStatsTime).count();
                        
                        if (timeSinceLastStats >= STATS_INTERVAL_SECONDS) {
                            lastStatsTime = now;
                            
                            std::cout << "\n=== Performance Statistics (Frame " << frameCount << ") ===" << std::endl;
                            std::cout << std::fixed << std::setprecision(3);
                            
                            // Real CPU-measured frame time (primary CPU metric)
                            std::cout << "  Real Frame Time (CPU): " << realFrameTimeMs << " ms" << std::endl;
                            std::cout << std::setprecision(3);
                            
                            // CPU breakdown
                            if (realFrameTimeMs > 0.0) {
                                std::cout << "  CPU Breakdown:" << std::endl;
                                std::cout << "    Matrix Prep: " << matrixPrepTimeMs << " ms (" 
                                          << std::setprecision(1) << (matrixPrepTimeMs * 100.0 / realFrameTimeMs) << "%)" << std::endl;
                                std::cout << std::setprecision(3);
                                std::cout << "    Descriptor Prep: " << descriptorPrepTimeMs << " ms (" 
                                          << std::setprecision(1) << (descriptorPrepTimeMs * 100.0 / realFrameTimeMs) << "%)" << std::endl;
                                std::cout << std::setprecision(3);
                                std::cout << "    Command Record: " << commandRecordTimeMs << " ms (" 
                                          << std::setprecision(1) << (commandRecordTimeMs * 100.0 / realFrameTimeMs) << "%)" << std::endl;
                                std::cout << std::setprecision(3);
                            }
                            std::cout << "  Threads Used: " << numThreads << " / " << std::thread::hardware_concurrency() << std::endl;
                            
                            // Segment information
                            if (!segmentRanges_.empty()) {
                                std::cout << "  Segments: " << segmentRanges_.size() << " (" << objects_.size() << " total objects)" << std::endl;
                                if (segmentRanges_.size() <= 10) {  // Only show details if not too many segments
                                    for (const auto& seg : segmentRanges_) {
                                        std::cout << "    " << seg.name << ": " << (seg.endIndex - seg.startIndex) << " objects" << std::endl;
                                    }
                                }
                            }
                            
                            // GPU-measured times (from timestamp queries); collected for both classic and mesh-shading builds
                            if (renderer_.getProfiler() && renderer_.getProfiler()->isSupported()) {
                                uint32_t previousFrameIndex = (frameindex + VKSwapchain::MAX_FRAMES_IN_FLIGHT - 1) % VKSwapchain::MAX_FRAMES_IN_FLIGHT;
                                VKProfiler::ProfileResults results = renderer_.getProfiler()->getResults(previousFrameIndex);
                                
                                if (results.isValid) {
                                    // Primary GPU metric: draw time (ms) for pipeline comparison
                                    std::cout << "  Draw Time (GPU): " << results.drawTimeMs << " ms" << std::endl;
                                    std::cout << "  GPU Frame Time: " << results.frameTimeMs << " ms" << std::endl;
                                    std::cout << "  RenderPass Time: " << results.renderPassTimeMs << " ms" << std::endl;
                                }
                            }
                            
                            // GPU hardware statistics (from nvidia-smi)
                            if (gpuMonitor.isAvailable()) {
                                VKGpuMonitor::GpuStats gpuStats = gpuMonitor.getCurrentStats();
                                if (gpuStats.isValid) {
                                    std::cout << std::setprecision(0);
                                    if (gpuStats.utilizationPercent >= 0) {
                                        std::cout << "  GPU Utilization: " << gpuStats.utilizationPercent << "%" << std::endl;
                                    }
                                    if (gpuStats.temperatureCelsius >= 0) {
                                        std::cout << "  GPU Temperature: " << gpuStats.temperatureCelsius << "°C" << std::endl;
                                    }
                                    if (gpuStats.memoryTotalBytes > 0) {
                                        std::cout << std::fixed << std::setprecision(1);
                                        double memoryUsedGB = static_cast<double>(gpuStats.memoryUsedBytes) / (1024.0 * 1024.0 * 1024.0);
                                        double memoryTotalGB = static_cast<double>(gpuStats.memoryTotalBytes) / (1024.0 * 1024.0 * 1024.0);
                                        std::cout << "  VRAM Usage: " << memoryUsedGB << " GB / " << memoryTotalGB << " GB" << std::endl;
                                    }
                                    if (gpuStats.clockGraphicsMhz >= 0) {
                                        std::cout << std::setprecision(0);
                                        std::cout << "  GPU Clock: " << gpuStats.clockGraphicsMhz << " MHz" << std::endl;
                                    }
                                    std::cout << std::setprecision(3);
                                }
                            }
                            
                            std::cout << "======================================" << std::endl;
                        }

                        if (lodSettings_.debugLogs) {
                            const double timeSinceLodDebug = std::chrono::duration<double>(now - lastLodDebugTime).count();
                            if (timeSinceLodDebug >= lodSettings_.debugIntervalSeconds) {
                                lastLodDebugTime = now;
                                if (!lodDebugGroupsPerLevelFrame.empty()) {
                                    uint64_t totalGroups = 0;
                                    uint64_t totalMeshlets = 0;
                                    std::cout << "\n[LOD DEBUG] Frame " << frameCount
                                              << " hierarchyObjects=" << lodDebugObjectsWithHierarchyFrame
                                              << " mixedObjects=" << lodDebugObjectsMixedFrame << std::endl;
                                    std::cout << "  Selected groups per level:";
                                    for (size_t li = 0; li < lodDebugGroupsPerLevelFrame.size(); li++) {
                                        totalGroups += lodDebugGroupsPerLevelFrame[li];
                                        totalMeshlets += lodDebugMeshletsPerLevelFrame[li];
                                        std::cout << " L" << li << "=" << lodDebugGroupsPerLevelFrame[li];
                                    }
                                    std::cout << std::endl;
                                    std::cout << "  Selected meshlets per level:";
                                    for (size_t li = 0; li < lodDebugMeshletsPerLevelFrame.size(); li++) {
                                        std::cout << " L" << li << "=" << lodDebugMeshletsPerLevelFrame[li];
                                    }
                                    std::cout << std::endl;
                                    std::cout << "  Totals: groups=" << totalGroups
                                              << ", meshlets=" << totalMeshlets << std::endl;
                                    if (lodDebugObjectsMixedFrame > 0 && lodDebugMixedProjectedMin <= lodDebugMixedProjectedMax) {
                                        std::cout << "  Mixed projected radius px range: min=" << lodDebugMixedProjectedMin
                                                  << ", max=" << lodDebugMixedProjectedMax << std::endl;
                                        std::cout << "  Mixed transition0 threshold px: "
                                                  << std::max(8.0f, lodSettings_.targetPixels * 64.0f * lodSettings_.mixedResponse)
                                                  << " (response=" << lodSettings_.mixedResponse << ")" << std::endl;
                                    }
                                    for (const auto& sample : lodDebugObjectSamples) {
                                        std::cout << "  " << sample << std::endl;
                                    }
                                } else {
                                    std::cout << "\n[LOD DEBUG] Frame " << frameCount
                                              << " no hierarchical groups selected in this frame." << std::endl;
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "\n=== RENDER LOOP ERROR (Frame " << frameCount << ") ===" << std::endl;
                    std::cerr << "Error: " << e.what() << std::endl;
                    std::cerr << "Attempting to gracefully shut down..." << std::endl;
                    
                    // Print current memory state
                    VKDevice::Device::MemoryInfo memInfo = device_.getMemoryInfo();
                    VKUtils::SystemMemoryInfo sysMemInfo = VKUtils::getSystemMemoryInfo();
                    
                    std::cerr << "\nCurrent Memory State:" << std::endl;
                    for (size_t i = 0; i < memInfo.heaps.size(); ++i) {
                        const auto& heap = memInfo.heaps[i];
                        std::cerr << "  Heap " << i << " (" 
                                 << (heap.isDeviceLocal ? "VRAM" : "System RAM") << "):" << std::endl;
                        std::cerr << "    Total: " << (heap.totalSize / (1024 * 1024)) << " MB ("
                                 << (heap.totalSize / (1024ULL * 1024 * 1024)) << " GB)" << std::endl;
                        std::cerr << "    Available: " << (heap.availableSize / (1024 * 1024)) << " MB" << std::endl;
                    }
                    
                    if (sysMemInfo.isValid) {
                        std::cerr << "  System RAM:" << std::endl;
                        std::cerr << "    Total: " << (sysMemInfo.totalRam / (1024 * 1024)) << " MB ("
                                 << (sysMemInfo.totalRam / (1024ULL * 1024 * 1024)) << " GB)" << std::endl;
                        std::cerr << "    Available: " << (sysMemInfo.availableRam / (1024 * 1024)) << " MB" << std::endl;
                        std::cerr << "    Used: " << (sysMemInfo.usedRam / (1024 * 1024)) << " MB" << std::endl;
                    }
                    std::cerr << "==========================================\n" << std::endl;
                    
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "\n=== FATAL ERROR IN RENDER LOOP ===" << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
            std::cerr << "===================================\n" << std::endl;
        }

        vkDeviceWaitIdle(device_.get_logic());
        
        // Explicitly clear objects to ensure proper cleanup
        objects_.clear();
    }

    void App::loadObjects()
    {
        objects_.clear();
        segmentRanges_.clear();

        // Scene selector:
        //  - SCENE_MODE=1 or SKY_8X8      -> existing 8x8 skyscraper scene
        //  - SCENE_MODE=2 or MIXED_20X20  -> single hotel instance
        std::string sceneMode = "SKY_8X8";
        if (const char* sceneModeEnv = std::getenv("SCENE_MODE")) {
            if (std::strlen(sceneModeEnv) > 0) {
                sceneMode = sceneModeEnv;
            }
        }
        std::string sceneModeUpper = sceneMode;
        std::transform(sceneModeUpper.begin(), sceneModeUpper.end(), sceneModeUpper.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const bool useMixedScene = (sceneModeUpper == "2" || sceneModeUpper == "MIXED_20X20" || sceneModeUpper == "MIXED");
        std::cout << "SCENE_MODE=" << sceneMode << " -> "
                  << (useMixedScene ? "Mixed 10x10" : "Skyscraper 8x8")
                  << std::endl;

        // Asset paths.
        const std::string skyscraperPath = "../../src/src/assets/skycreeper/London_Bridge_Quarter_Combined2resave_in_6-0_verified.obj";
        const std::string hotelPath = "../../src/src/assets/hotel/Hotel_Orig.obj";

        const float rotateX = glm::radians(90.0f);  // Z-up (IFC/OBJ) -> Y-up

        auto addStaticObject = [&](const std::shared_ptr<VKModel::Model>& model, const glm::vec3& p, float scale = 1.0f) {
            auto obj = VKObject::Object::createObject();
            obj.model_ = model;
            obj.transform3D_.translation = p;
            obj.transform3D_.scale = glm::vec3{scale};
            obj.transform3D_.rotation = {rotateX, 0.0f, 0.0f};
            obj.transform3D_.isStatic = true;
            objects_.push_back(std::move(obj));
        };

        // Compute world footprint area (on XZ plane) after rotateX transform.
        auto footprintAreaXZ = [&](const std::shared_ptr<VKModel::Model>& model) {
            glm::vec3 bmin, bmax;
            model->getBoundingBox(bmin, bmax);
            const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), rotateX, glm::vec3(1.0f, 0.0f, 0.0f));
            std::array<glm::vec3, 8> corners = {
                glm::vec3{bmin.x, bmin.y, bmin.z},
                glm::vec3{bmax.x, bmin.y, bmin.z},
                glm::vec3{bmin.x, bmax.y, bmin.z},
                glm::vec3{bmax.x, bmax.y, bmin.z},
                glm::vec3{bmin.x, bmin.y, bmax.z},
                glm::vec3{bmax.x, bmin.y, bmax.z},
                glm::vec3{bmin.x, bmax.y, bmax.z},
                glm::vec3{bmax.x, bmax.y, bmax.z},
            };
            float minX = std::numeric_limits<float>::max();
            float maxX = -std::numeric_limits<float>::max();
            float minZ = std::numeric_limits<float>::max();
            float maxZ = -std::numeric_limits<float>::max();
            for (const auto& c : corners) {
                const glm::vec3 r = glm::vec3(rot * glm::vec4(c, 1.0f));
                minX = std::min(minX, r.x);
                maxX = std::max(maxX, r.x);
                minZ = std::min(minZ, r.z);
                maxZ = std::max(maxZ, r.z);
            }
            return std::max(1.0f, (maxX - minX) * (maxZ - minZ));
        };

        // Optional LOD cache per model type (fallback to OBJ if not provided):
        // - VK_MODEL_LOD_CACHE         : skyscraper
        // - VK_HOTEL_LOD_CACHE         : hotel
        auto loadModelWithOptionalLod = [&](const std::string& objPath,
                                            const char* lodEnvName,
                                            const char* readableName) -> std::shared_ptr<VKModel::Model> {
            const char* lodPathEnv = std::getenv(lodEnvName);
            if (lodPathEnv && std::strlen(lodPathEnv) > 0) {
                auto m = VKModel::Model::createModelFromLodCache(device_, std::string(lodPathEnv), "");
                std::cout << "Loaded " << readableName << " from LOD cache: " << lodPathEnv << std::endl;
                return m;
            }
            auto m = VKModel::Model::createModelfromFile(device_, objPath, "");
            std::cout << "Loaded " << readableName << " OBJ model" << std::endl;
            return m;
        };

        // Skyscraper model source (OBJ fallback if VK_MODEL_LOD_CACHE isn't provided).
        std::shared_ptr<VKModel::Model> model_skyscraper;
        try {
            model_skyscraper = loadModelWithOptionalLod(skyscraperPath, "VK_MODEL_LOD_CACHE", "skyscraper");
        } catch (const std::exception& e) {
            std::cerr << "Failed to load skyscraper: " << e.what() << std::endl;
            return;
        }

        // Build scene basis:
        // - armA goes opposite to initial camera direction (+Z),
        // - armB follows last logged camera azimuth, orthogonalized to armA.
        const glm::vec3 armA = glm::vec3(0.0f, 0.0f, 1.0f);  // opposite to initial camera at (0,0,-350)
        const glm::vec3 lastCameraPos = glm::vec3(-24.897f, 0.0f, -382.454f);
        glm::vec3 armB = glm::normalize(glm::vec3(lastCameraPos.x, 0.0f, lastCameraPos.z));
        armB = armB - armA * glm::dot(armB, armA);
        if (glm::dot(armB, armB) < 1e-6f) armB = glm::vec3(-1.0f, 0.0f, 0.0f);
        armB = glm::normalize(armB);
        const glm::vec3 origin(0.0f, 0.0f, 0.0f);

        if (!useMixedScene) {
            // Scene 1: existing 8x8 skyscraper grid (unchanged behavior).
            const float spacing = 420.0f;
            constexpr int gridSize = 8;
            for (int i = 0; i < gridSize; i++) {
                for (int j = 0; j < gridSize; j++) {
                    const glm::vec3 p = origin
                        + armA * (spacing * static_cast<float>(i))
                        + armB * (spacing * static_cast<float>(j));
                    addStaticObject(model_skyscraper, p);
                }
            }
            std::cout << "Total objects: " << objects_.size()
                      << " (8x8 skyscraper instances, one shared model)" << std::endl;
        } else {
            // Scene 2: single hotel only (no stadiums/skyscrapers).
            std::shared_ptr<VKModel::Model> model_hotel;
            try {
                model_hotel = loadModelWithOptionalLod(hotelPath, "VK_HOTEL_LOD_CACHE", "hotel");
            } catch (const std::exception& e) {
                std::cerr << "Failed to load hotel model: " << e.what() << std::endl;
                return;
            }

            const glm::vec3 hotelPos = origin;
            addStaticObject(model_hotel, hotelPos, 4.0f);
            std::cout << "Total objects: " << objects_.size()
                      << " (single hotel instance, scale=4x)" << std::endl;
        }

        // Final memory status for both classic and mesh-shading builds
        VKUtils::SystemMemoryInfo sysMem = VKUtils::getSystemMemoryInfo();
        std::cout << "\n=== Final Memory Status ===" << std::endl;
        device_.printMemoryInfo();
        if (sysMem.isValid) {
            std::cout << "System RAM:" << std::endl;
            std::cout << "  Total: " << (sysMem.totalRam / (1024ULL * 1024 * 1024)) << " GB ("
                      << (sysMem.totalRam / (1024 * 1024)) << " MB)" << std::endl;
            std::cout << "  Available: " << (sysMem.availableRam / (1024ULL * 1024 * 1024)) << " GB ("
                      << (sysMem.availableRam / (1024 * 1024)) << " MB)" << std::endl;
            std::cout << "  Used: " << (sysMem.usedRam / (1024ULL * 1024 * 1024)) << " GB ("
                      << (sysMem.usedRam / (1024 * 1024)) << " MB)" << std::endl;
        }
        std::cout << "============================\n" << std::endl;
    }

}   //  end of VKEngine namespace
