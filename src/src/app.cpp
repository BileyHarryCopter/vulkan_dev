#include "app.hpp"

#include "render_system.hpp"
#include "utility.hpp"
#include "gpu_monitor.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cassert>
#include <stdexcept>
#include <string>
#include <iomanip>
#include <thread>
#include <algorithm>
#include <execution>
#include <mutex>
#include <atomic>

namespace VKEngine
{

    void App::run()
    {
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


        //  creating layout for GLOBAL set and it respectively
#ifdef USE_MESH_SHADING
        // For mesh shading: UBO, textures, storage buffers for meshlets, and object matrices
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet structures
            .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet vertices
            .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet triangles
            .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // vertex data
            .addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)  // object matrices
            .build();
#else
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
            .addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)  // object matrices
            .build();
#endif
        std::vector<VkDescriptorSet> allDescriptorsets(VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size()); 
        int descriptorSetIndex = 0;
        for (int frame = 0; frame < VKSwapchain::MAX_FRAMES_IN_FLIGHT; frame++) 
        {
            auto bufferInfo = ubobuffs[frame]->descriptorInfo();
            // Object matrices storage buffer (shared across all objects in this frame)
            auto objectMatricesBufferInfo = objectMatricesBuffers_[frame]->descriptorInfo();

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
                
                VKDescriptors::DescriptorWriter writer(*setlayout, *globalPool);
                writer.writeBuffer(0, &bufferInfo)  // UBO
                      .writeImage(1, &imageInfo)    // Texture
                      .writeBuffer(2, &meshletBufferInfo)  // Meshlet structures
                      .writeBuffer(3, &meshletVerticesBufferInfo)  // Meshlet vertex indices
                      .writeBuffer(4, &meshletTrianglesBufferInfo)  // Meshlet triangle indices
                      .writeBuffer(5, &vertexDataBufferInfo)  // Vertex data
                      .writeBuffer(6, &objectMatricesBufferInfo);  // Object matrices (shared)
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
                      .writeBuffer(6, &objectMatricesBufferInfo);  // Object matrices (shared)
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
        viewerObject.transform3D_.translation = {0.0f, 0.0f, -10.0f};
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
                        
                        // Batch flush both buffers for better performance
                        std::vector<VKBuffmanager::Buffmanager*> buffersToFlush = {
                            ubobuffs[frameindex].get(),
                            objectMatricesBuffers_[frameindex].get()
                        };
                        VKBuffmanager::Buffmanager::flushMultiple(device_, buffersToFlush);

                        //  renderer - measure command recording time
                        auto commandRecordStart = std::chrono::high_resolution_clock::now();
                        
                        // Record secondary command buffers BEFORE render pass (if parallel recording is enabled)
                        if (renderSystem.isParallelRecordingEnabled()) {
                            renderSystem.recordSecondaryCommandBuffers(frameinfo, objects_);
                        }
                        
                        // Begin render pass (with secondary command buffers flag if needed)
                        renderer_.beginSwapchainRenderpass(commandBuffer, renderSystem.isParallelRecordingEnabled());
                        
                        // Render objects (will execute secondary buffers if parallel, or record inline if sequential)
                        renderSystem.renderObjects(frameinfo, objects_);
                        
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
                            
                            // Real CPU-measured frame time
                            std::cout << "  Real Frame Time (CPU): " << realFrameTimeMs << " ms" << std::endl;
                            std::cout << "  Real FPS (CPU): " << std::setprecision(1) << realFps << std::endl;
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
                            
                            // GPU-measured times (from timestamp queries)
                            if (renderer_.getProfiler() && renderer_.getProfiler()->isSupported()) {
                                uint32_t previousFrameIndex = (frameindex + VKSwapchain::MAX_FRAMES_IN_FLIGHT - 1) % VKSwapchain::MAX_FRAMES_IN_FLIGHT;
                                VKProfiler::ProfileResults results = renderer_.getProfiler()->getResults(previousFrameIndex);
                                
                                if (results.isValid) {
                                    double gpuFps = results.frameTimeMs > 0.0 ? 1000.0 / results.frameTimeMs : 0.0;
                                    
                                    std::cout << "  GPU Execution Time: " << results.frameTimeMs << " ms" << std::endl;
                                    std::cout << "  GPU FPS: " << std::setprecision(1) << gpuFps << std::endl;
                                    std::cout << std::setprecision(3);
                                    std::cout << "  RenderPass Time: " << results.renderPassTimeMs << " ms" << std::endl;
                                    std::cout << "  Draw Time: " << results.drawTimeMs << " ms" << std::endl;
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
#ifdef USE_MESH_SHADING
        VKUtils::SystemMemoryInfo sysMem;
        try {
            std::string buddhaPath = "../../src/src/assets/buddha.obj";
            std::shared_ptr<VKModel::Model> model_buddha = 
                VKModel::Model::createModelfromFile(device_, buddhaPath, "");
            
            const int NUM_INSTANCES = 20;
            const float SPACING = 1.0f;
            
            for (int i = 0; i < NUM_INSTANCES; i++) {
                auto obj_buddha = VKObject::Object::createObject();
                obj_buddha.model_ = model_buddha;
                obj_buddha.transform3D_.translation = {i * SPACING, 0.0f, 0.0f};
                obj_buddha.transform3D_.scale = glm::vec3{1.0f};
                obj_buddha.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
                obj_buddha.transform3D_.isStatic = true;
                objects_.push_back(std::move(obj_buddha));
            }

            for (int i = 0; i < NUM_INSTANCES; i++) {
                auto obj_buddha = VKObject::Object::createObject();
                obj_buddha.model_ = model_buddha;
                obj_buddha.transform3D_.translation = {i * SPACING, 0.0f, 1.0f};
                obj_buddha.transform3D_.scale = glm::vec3{1.0f};
                obj_buddha.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
                obj_buddha.transform3D_.isStatic = true;
                objects_.push_back(std::move(obj_buddha));
            }

            for (int i = 0; i < NUM_INSTANCES; i++) {
                auto obj_buddha = VKObject::Object::createObject();
                obj_buddha.model_ = model_buddha;
                obj_buddha.transform3D_.translation = {i * SPACING, 0.0f, -1.0f};
                obj_buddha.transform3D_.scale = glm::vec3{1.0f};
                obj_buddha.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
                obj_buddha.transform3D_.isStatic = true;
                objects_.push_back(std::move(obj_buddha));
            }
            
            std::cout << "Loaded Buddha × " << NUM_INSTANCES << " instances" << std::endl;
            std::cout << "Total objects: " << objects_.size() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load Buddha: " << e.what() << std::endl;
        }
        
        // Print final memory status
        std::cout << "\n=== Final Memory Status ===" << std::endl;
        device_.printMemoryInfo();
        
        sysMem = VKUtils::getSystemMemoryInfo();
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
        
#else
        try {
            std::string buddhaPath = "../../src/src/assets/buddha.obj";
            std::shared_ptr<VKModel::Model> model_buddha = 
                VKModel::Model::createModelfromFile(device_, buddhaPath, "");
            
            const int NUM_INSTANCES = 20;
            const float SPACING = 1.0f;
            
            for (int i = 0; i < NUM_INSTANCES; i++) {
                auto obj_buddha = VKObject::Object::createObject();
                obj_buddha.model_ = model_buddha;
                obj_buddha.transform3D_.translation = {i * SPACING, 0.0f, 0.0f};
                obj_buddha.transform3D_.scale = glm::vec3{1.0f};
                obj_buddha.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
                obj_buddha.transform3D_.isStatic = true;
                objects_.push_back(std::move(obj_buddha));
            }

            for (int i = 0; i < NUM_INSTANCES; i++) {
                auto obj_buddha = VKObject::Object::createObject();
                obj_buddha.model_ = model_buddha;
                obj_buddha.transform3D_.translation = {i * SPACING, 0.0f, 1.0f};
                obj_buddha.transform3D_.scale = glm::vec3{1.0f};
                obj_buddha.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
                obj_buddha.transform3D_.isStatic = true;
                objects_.push_back(std::move(obj_buddha));
            }

            for (int i = 0; i < NUM_INSTANCES; i++) {
                auto obj_buddha = VKObject::Object::createObject();
                obj_buddha.model_ = model_buddha;
                obj_buddha.transform3D_.translation = {i * SPACING, 0.0f, -1.0f};
                obj_buddha.transform3D_.scale = glm::vec3{1.0f};
                obj_buddha.transform3D_.rotation = {0.0f, 0.0f, 0.0f};
                obj_buddha.transform3D_.isStatic = true;
                objects_.push_back(std::move(obj_buddha));
            }
            
            std::cout << "Loaded Buddha × " << NUM_INSTANCES << " instances" << std::endl;
            std::cout << "Total objects: " << objects_.size() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load Buddha: " << e.what() << std::endl;
        }
#endif

    }

}   //  end of VKEngine namespace
