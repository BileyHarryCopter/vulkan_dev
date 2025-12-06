#include "app.hpp"

#include "render_system.hpp"
#include "utility.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cassert>
#include <stdexcept>
#include <string>
#include <iomanip>

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


        //  creating layout for GLOBAL set and it respectively
#ifdef USE_MESH_SHADING
        // For mesh shading: UBO, textures, and storage buffers for meshlets
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet structures
            .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet vertices
            .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // meshlet triangles
            .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT, 1)  // vertex data
            .build();
#else
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_).addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
                                                                             .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1).build();
#endif
        std::vector<VkDescriptorSet> allDescriptorsets(VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size()); 
        int descriptorSetIndex = 0;
        for (int frame = 0; frame < VKSwapchain::MAX_FRAMES_IN_FLIGHT; frame++) 
        {
            auto bufferInfo = ubobuffs[frame]->descriptorInfo();

            for (auto& obj : objects_)
            {
#ifdef USE_MESH_SHADING
                // For mesh shading: UBO, textures, and storage buffers for meshlets
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
                      .writeBuffer(5, &vertexDataBufferInfo);  // Vertex data
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
                VKDescriptors::DescriptorWriter(*setlayout, *globalPool).writeBuffer(0, &bufferInfo).writeImage(1, &imageInfo).build(allDescriptorsets[descriptorSetIndex]);
#endif
                descriptorSetIndex++;  // Increment the index explicitly
            }
        }

        auto descriptorSetLayouts = std::vector<VkDescriptorSetLayout> {setlayout->getDescriptorSetLayout()};
        VKRenderSystem::RenderSystem renderSystem {device_, renderer_.getSwapChainRenderPass(), descriptorSetLayouts};


        VKCamera::Camera camera{};

        auto viewerObject =    VKObject::Object::createObject();
        viewerObject.transform3D_.translation = {0.0f, 48000.0f, -42000.0f};
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
        int frameCount = 0;

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
                    camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100000.f);

                    // Debug: Log camera position every 60 frames (approximately once per second at 60 FPS)
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

                    if (auto commandBuffer = renderer_.beginFrame())
                    {
                        int frameindex = renderer_.getframeindex();

                        std::vector<VkDescriptorSet> descriptorsets {};
                        for (int i = 0, len = objects_.size(); i < objects_.size(); i++) {
                            int descriptorIndex = len * frameindex + i;
                            if (descriptorIndex >= allDescriptorsets.size()) {
                                throw std::runtime_error("Descriptor set index out of bounds when building frame descriptors!");
                            }
                            descriptorsets.push_back(allDescriptorsets[descriptorIndex]);
                        }
                        

                        VKRenderSystem::FrameInfo frameinfo {frameindex, frameTime, commandBuffer, camera, descriptorsets};

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
                        ubobuffs[frameindex]->flush();

                        //  renderer
                        renderer_.beginSwapchainRenderpass(commandBuffer);
                        renderSystem.renderObjects(frameinfo, objects_);
                        renderer_.endSwapchainRenderpass(commandBuffer);
                        renderer_.endFrame();
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
                    
                    // Break out of the loop to allow cleanup
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
        // Print initial memory information
        std::cout << "\n=== Initial Memory Status ===" << std::endl;
        device_.printMemoryInfo();
        
        VKUtils::SystemMemoryInfo sysMem = VKUtils::getSystemMemoryInfo();
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
        
        // Load Boston segments: H_2, H_3, H_4, H_5, H_6, etc.
        struct SegmentInfo {
            char segmentLetter;  // 'H', 'I', 'J', etc.
            int segmentNumber;
            int numBuildings;
        };
        
        std::vector<SegmentInfo> segments = {
            // {'H', 5, 1510},
            // {'H', 6, 1841},
            // {'H', 7, 5323},
            // {'H', 8, 5067},
            {'H', 3, 2041},
            {'H', 4, 1750},
            // {'H', 5, 1510},
            {'I', 3, 1234},
            {'I', 4, 869},
            // {'I', 5, 1532},
            // {'J', 3, 2310},
            // {'J', 4, 228},
            // {'J', 5, 941}
        };
        
        for (const auto& seg : segments) {
            std::string segmentName = "BOS_" + std::string(1, seg.segmentLetter) + "_" + std::to_string(seg.segmentNumber);
            
            // Load main model for segment
            try {
                std::string mainObjPath = "../../src/src/assets/Boston/" + segmentName + "/" + segmentName + "/" + segmentName + ".obj";
                std::string mainTexturePath = "../../src/src/assets/Boston/" + segmentName + "/" + segmentName + "/" + segmentName + ".JPG";
                std::shared_ptr<VKModel::Model> model_main = VKModel::Model::createModelfromFile(
                    device_, mainObjPath, mainTexturePath
                );
                auto obj_main = VKObject::Object::createObject();
                obj_main.model_ = model_main;
                obj_main.transform3D_.translation = {0.0f, 0.0f, 0.0f};
                obj_main.transform3D_.scale = glm::vec3{1.0f};
                obj_main.transform3D_.rotation = {0.0f, 1.57f, 0.0f};
                objects_.push_back(std::move(obj_main));
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to load " << segmentName << " main model: " << e.what() << std::endl;
            }
            
            // Load building models for segment
            std::cout << "Loading " << seg.numBuildings << " " << segmentName << " building models..." << std::endl;
            int segmentLoadedCount = 0;
            const int memoryCheckInterval = 500;
            
            for (int i = 0; i < seg.numBuildings; i++) {
                std::string building_folder = segmentName + "_" + std::to_string(i);
                std::string obj_path = "../../src/src/assets/Boston/" + segmentName + "/objz/" + 
                                       building_folder + "/" + building_folder + ".obj";
                try {
                    std::shared_ptr<VKModel::Model> model_building = VKModel::Model::createModelfromFile(
                        device_, obj_path, ""
                    );
                    auto obj_building = VKObject::Object::createObject();
                    obj_building.model_ = model_building;
                    obj_building.transform3D_.translation = {0.0f, 0.0f, 0.0f};
                    obj_building.transform3D_.scale = glm::vec3{1.0f};
                    obj_building.transform3D_.rotation = {0.0f, 1.57f, 0.0f};
                    objects_.push_back(std::move(obj_building));
                    segmentLoadedCount++;
                    
                    // Periodic memory check
                    if ((i + 1) % memoryCheckInterval == 0 || i == seg.numBuildings - 1) {
                        VKUtils::SystemMemoryInfo sysMem = VKUtils::getSystemMemoryInfo();
                        if (sysMem.isValid) {
                            double ramUsagePercent = (double)sysMem.usedRam * 100.0 / sysMem.totalRam;
                            std::cout << "  Progress: " << (i + 1) << "/" << seg.numBuildings 
                                      << " models processed (" << segmentLoadedCount << " loaded)" << std::endl;
                            std::cout << "  System RAM: " << std::fixed << std::setprecision(1) << ramUsagePercent 
                                      << "% used (" << (sysMem.usedRam / (1024ULL * 1024 * 1024)) << " GB / "
                                      << (sysMem.totalRam / (1024ULL * 1024 * 1024)) << " GB)" << std::endl;
                            
                            if (ramUsagePercent > 80.0) {
                                std::cerr << "  WARNING: System RAM usage above 80%!" << std::endl;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Warning: Failed to load building " << building_folder << ": " << e.what() << std::endl;
                }
            }
            std::cout << "Loaded " << segmentName << " segment (" << segmentLoadedCount << " buildings)" << std::endl;
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
        // For traditional vertex/fragment pipeline: load Boston segments
        // Print initial memory information
        std::cout << "\n=== Initial Memory Status ===" << std::endl;
        device_.printMemoryInfo();
        
        VKUtils::SystemMemoryInfo sysMem = VKUtils::getSystemMemoryInfo();
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
        
        // Load Boston segments: H_2, H_3, H_4, H_5, H_6, etc.
        struct SegmentInfo {
            char segmentLetter;  // 'H', 'I', 'J', etc.
            int segmentNumber;
            int numBuildings;
        };
        
        std::vector<SegmentInfo> segments = {
            // {'H', 5, 1510},
            // {'H', 6, 1841},
            // {'H', 7, 5323},
            // {'H', 8, 5067},
            {'H', 3, 2041},
            {'H', 4, 1750},
            // {'H', 5, 1510},
            {'I', 3, 1234},
            {'I', 4, 869},
            // {'I', 5, 1532},
            // {'J', 3, 2310},
            // {'J', 4, 228},
            // {'J', 5, 941}
        };
        
        for (const auto& seg : segments) {
            std::string segmentName = "BOS_" + std::string(1, seg.segmentLetter) + "_" + std::to_string(seg.segmentNumber);
            
            // Load main model for segment
            try {
                std::string mainObjPath = "../../src/src/assets/Boston/" + segmentName + "/" + segmentName + "/" + segmentName + ".obj";
                std::string mainTexturePath = "../../src/src/assets/Boston/" + segmentName + "/" + segmentName + "/" + segmentName + ".JPG";
                std::shared_ptr<VKModel::Model> model_main = VKModel::Model::createModelfromFile(
                    device_, mainObjPath, mainTexturePath
                );
                auto obj_main = VKObject::Object::createObject();
                obj_main.model_ = model_main;
                obj_main.transform3D_.translation = {0.0f, 0.0f, 0.0f};
                obj_main.transform3D_.scale = glm::vec3{1.0f};
                obj_main.transform3D_.rotation = {0.0f, 1.57f, 0.0f};
                objects_.push_back(std::move(obj_main));
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to load " << segmentName << " main model: " << e.what() << std::endl;
            }
            
            // Load building models for segment
            std::cout << "Loading " << seg.numBuildings << " " << segmentName << " building models..." << std::endl;
            int segmentLoadedCount = 0;
            const int memoryCheckInterval = 500;
            
            for (int i = 0; i < seg.numBuildings; i++) {
                std::string building_folder = segmentName + "_" + std::to_string(i);
                std::string obj_path = "../../src/src/assets/Boston/" + segmentName + "/objz/" + 
                                       building_folder + "/" + building_folder + ".obj";
                try {
                    std::shared_ptr<VKModel::Model> model_building = VKModel::Model::createModelfromFile(
                        device_, obj_path, ""
                    );
                    auto obj_building = VKObject::Object::createObject();
                    obj_building.model_ = model_building;
                    obj_building.transform3D_.translation = {0.0f, 0.0f, 0.0f};
                    obj_building.transform3D_.scale = glm::vec3{1.0f};
                    obj_building.transform3D_.rotation = {0.0f, 1.57f, 0.0f};
                    objects_.push_back(std::move(obj_building));
                    segmentLoadedCount++;
                    
                    // Periodic memory check
                    if ((i + 1) % memoryCheckInterval == 0 || i == seg.numBuildings - 1) {
                        VKUtils::SystemMemoryInfo sysMem = VKUtils::getSystemMemoryInfo();
                        if (sysMem.isValid) {
                            double ramUsagePercent = (double)sysMem.usedRam * 100.0 / sysMem.totalRam;
                            std::cout << "  Progress: " << (i + 1) << "/" << seg.numBuildings 
                                      << " models processed (" << segmentLoadedCount << " loaded)" << std::endl;
                            std::cout << "  System RAM: " << std::fixed << std::setprecision(1) << ramUsagePercent 
                                      << "% used (" << (sysMem.usedRam / (1024ULL * 1024 * 1024)) << " GB / "
                                      << (sysMem.totalRam / (1024ULL * 1024 * 1024)) << " GB)" << std::endl;
                            
                            if (ramUsagePercent > 80.0) {
                                std::cerr << "  WARNING: System RAM usage above 80%!" << std::endl;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Warning: Failed to load building " << building_folder << ": " << e.what() << std::endl;
                }
            }
            std::cout << "Loaded " << segmentName << " segment (" << segmentLoadedCount << " buildings)" << std::endl;
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
#endif

    }

}   //  end of VKEngine namespace
