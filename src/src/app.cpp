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
        auto globalsetlayout = VKDescriptors::DescriptorSetLayout::Builder(device_).addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1).build();
        std::vector<VkDescriptorSet> globaldescriptorsets(VKSwapchain::MAX_FRAMES_IN_FLIGHT); 
        for (int i = 0; i < globaldescriptorsets.size(); i++)
        {
            auto bufferInfo = ubobuffs[i]->descriptorInfo();

            VKDescriptors::DescriptorWriter  (*globalsetlayout, *globalPool).writeBuffer(0, &bufferInfo).build(globaldescriptorsets[i]);
        }

        
        //  creating layout for OBJECT set and it respectively
        auto texturesetlayout = VKDescriptors::DescriptorSetLayout::Builder(device_).addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1).build();
        std::vector<VkDescriptorSet> texturedescriptorsets(VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size());
        int descriptorSetIndex = 0;
        for (int frame = 0; frame < VKSwapchain::MAX_FRAMES_IN_FLIGHT; frame++) 
        {
            for (auto& obj : objects_) 
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = obj.model_->getimgview();
                imageInfo.sampler = obj.model_->getsampler();

                std::cout << "Index of descriptorSetIndex: " << descriptorSetIndex << std::endl;

                VKDescriptors::DescriptorWriter(*texturesetlayout, *globalPool).writeImage(1, &imageInfo).build(texturedescriptorsets[descriptorSetIndex]);

                descriptorSetIndex++;  // Increment the index explicitly
            }
        }

        std::cout << "Globalsetlayout: " << globalsetlayout->getDescriptorSetLayout() << std::endl;
        std::cout << "Texturesetlayout: " << texturesetlayout->getDescriptorSetLayout() << std::endl;
        auto descriptorSetLayouts = std::vector<VkDescriptorSetLayout> {globalsetlayout->getDescriptorSetLayout() , texturesetlayout->getDescriptorSetLayout()};
        VKRenderSystem::RenderSystem renderSystem {device_, renderer_.getSwapChainRenderPass(), descriptorSetLayouts};


        VKCamera::Camera camera{};

        auto viewerObject =    VKObject::Object::createObject();
        VKKeyboardController::KeyboardController cameraController{};

        auto currentTime = std::chrono::high_resolution_clock::now();

        while(!window_.shouldClose())
        {
            glfwPollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;

            cameraController.moveInPlaneXZ(window_.get(), frameTime, viewerObject);
            camera.setViewYXZ(viewerObject.transform3D_.translation, viewerObject.transform3D_.rotation);

            float aspect = renderer_.getAspectRatio();
            camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 10.f);

            if (auto commandBuffer = renderer_.beginFrame())
            {
                std::cout << "\n\nSo, rendering\n";
                int frameindex = renderer_.getframeindex();
                std::cout << "Current frame index: " << frameindex << std::endl;

                std::vector<VkDescriptorSet> descriptorsets {};
                descriptorsets.push_back(globaldescriptorsets[frameindex]);
                
                if (descriptorsets[0] != globaldescriptorsets[frameindex])
                    std::cout << "What???\n";

                for (int i = 0, len = objects_.size(); i < objects_.size(); i++)
                {
                    std::cout << "Index of texture: " << len * frameindex + i << std::endl;
                    descriptorsets.push_back(texturedescriptorsets[len * frameindex + i]);
                }


                VKRenderSystem::FrameInfo frameinfo {frameindex, frameTime, commandBuffer, camera, descriptorsets};

                //  update Ubo
                GlobalUbo ubo{};
                ubo.projectionView = camera.getProjection() * camera.getView();

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
        std::shared_ptr<VKModel::Model> model_shrek       =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/viking_room.obj",
                                                                                                            "../../src/src/assets/viking_room.png");
        auto obj_shrek                     = VKObject::Object::createObject();
        obj_shrek.model_                   =                      model_shrek;
        obj_shrek.transform3D_.translation =              {-2.0f, 0.0f, 1.0f};
        obj_shrek.transform3D_.scale       =                glm::vec3{ -2.0f};

        objects_.push_back(std::move(obj_shrek));


        // std::shared_ptr<VKModel::Model> model_viking_room =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/viking_room.obj",
        //                                                                                                     "../../src/src/assets/viking_room.png");
        // auto obj_viking_room                     =   VKObject::Object::createObject();
        // obj_viking_room.model_                   =                  model_viking_room;
        // obj_viking_room.transform3D_.translation =                 {1.0f, 0.0f, 1.0f};
        // obj_viking_room.transform3D_.scale       =                    glm::vec3{2.0f};
        // obj_viking_room.transform3D_.rotation    =               {1.57f, 1.57f, 0.0f};

        // objects_.push_back(std::move(obj_viking_room));


        // std::shared_ptr<VKModel::Model> model_vase       =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/smooth_vase.obj", "");
        // auto obj_vase                     = VKObject::Object::createObject();
        // obj_vase.model_                   =                       model_vase;
        // obj_vase.transform3D_.translation =              {-2.0f, 0.0f, 1.0f};
        // obj_vase.transform3D_.scale       =                  glm::vec3{2.0f};

        // objects_.push_back(std::move(obj_vase));
    }

}   //  end of VKEngine namespace
