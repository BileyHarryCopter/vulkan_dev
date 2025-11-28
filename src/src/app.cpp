#include "app.hpp"

#include "render_system.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cassert>
#include <stdexcept>

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

        while(!window_.shouldClose())
        {
            glfwPollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;

            cameraController.moveInPlaneXZ(window_.get(), frameTime, viewerObject);
            camera.setViewYXZ(viewerObject.transform3D_.translation, viewerObject.transform3D_.rotation);

            // Update rotation for all objects (synchronous rotation around Y axis)
            const float rotationSpeed = 1.0f;  // radians per second
            for (auto& obj : objects_) {
                obj.transform3D_.rotation.y += rotationSpeed * frameTime;
                // Keep rotation in [0, 2π) range to prevent overflow
                obj.transform3D_.rotation.y = glm::mod(obj.transform3D_.rotation.y, glm::two_pi<float>());
            }

            float aspect = renderer_.getAspectRatio();
            camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

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

        }

        vkDeviceWaitIdle(device_.get_logic());
    }

    void App::loadObjects()
    {
#ifdef USE_MESH_SHADING
        // For mesh shading mode: load single Skull model without texture
        // Model will be automatically converted to meshlets and colored by index modulo 6
        std::shared_ptr<VKModel::Model> model_skull = VKModel::Model::createModelfromFile(
            device_,
            "../../src/src/assets/Skull/Skull.obj",
            ""  // Empty texture path - no texture will be loaded
        );
        auto obj_skull = VKObject::Object::createObject();
        obj_skull.model_ = model_skull;
        obj_skull.transform3D_.translation = {0.0f, 0.0f, 0.0f};
        obj_skull.transform3D_.scale = glm::vec3{0.8f};
        obj_skull.transform3D_.rotation = {1.57f, 1.57f, 0.0f};

        objects_.push_back(std::move(obj_skull));
#else
        // For traditional vertex/fragment pipeline: load multiple Skull models in a grid
        for (int i = 0; i < 10; i++)
        {
            for (int j = 0; j < 10; j++)
            {
                std::shared_ptr<VKModel::Model> model_viking_room = VKModel::Model::createModelfromFile(
                    device_,
                    "../../src/src/assets/Skull/Skull.obj",
                    "../../src/src/assets/Skull/Skull.jpg"
                );
                auto obj_viking_room = VKObject::Object::createObject();
                obj_viking_room.model_ = model_viking_room;
                obj_viking_room.transform3D_.translation = {i * 20.0f, j * 20.0f, 0.0f};
                obj_viking_room.transform3D_.scale = glm::vec3{0.8f};
                obj_viking_room.transform3D_.rotation = {1.57f, 1.57f, 0.0f};

                objects_.push_back(std::move(obj_viking_room));
            }
        }
#endif



        // std::shared_ptr<VKModel::Model> model_shrek1       =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/shrek.obj",
        //                                                                                                     "../../src/src/assets/shrek.png");
        // auto obj_shrek1                     = VKObject::Object::createObject();
        // obj_shrek1.model_                   =                     model_shrek1;
        // obj_shrek1.transform3D_.translation =              {0.0f, -1.0f, 0.0f};
        // obj_shrek1.transform3D_.scale       =                glm::vec3{ -0.8f};

        // objects_.push_back(std::move(obj_shrek1));



        // std::shared_ptr<VKModel::Model> model_shrek2       =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/shrek.obj",
        //                                                                                                     "../../src/src/assets/shrek.png");
        // auto obj_shrek2                     = VKObject::Object::createObject();
        // obj_shrek2.model_                   =                     model_shrek2;
        // obj_shrek2.transform3D_.translation =              {0.0f, 1.0f, 0.0f};
        // obj_shrek2.transform3D_.scale       =                glm::vec3{ 0.8f};

        // objects_.push_back(std::move(obj_shrek2));



        // std::shared_ptr<VKModel::Model> model_shrek3       =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/shrek.obj",
        //                                                                                                     "../../src/src/assets/shrek.png");
        // auto obj_shrek3                     = VKObject::Object::createObject();
        // obj_shrek3.model_                   =                     model_shrek3;
        // obj_shrek3.transform3D_.translation =               {2.0f, 1.0f, 0.0f};
        // obj_shrek3.transform3D_.scale       =                 glm::vec3{ -0.8f};

        // objects_.push_back(std::move(obj_shrek3));



        // std::shared_ptr<VKModel::Model> model_shrek4       =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/shrek.obj",
        //                                                                                                     "../../src/src/assets/shrek.png");
        // auto obj_shrek4                     = VKObject::Object::createObject();
        // obj_shrek4.model_                   =                     model_shrek4;
        // obj_shrek4.transform3D_.translation =               {-2.0f, -1.0f, 0.0f};
        // obj_shrek4.transform3D_.scale       =                 glm::vec3{ 0.8f};

        // objects_.push_back(std::move(obj_shrek4));

    }

}   //  end of VKEngine namespace
