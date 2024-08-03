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
        auto setlayout = VKDescriptors::DescriptorSetLayout::Builder(device_).addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
                                                                             .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1).build();
        std::vector<VkDescriptorSet> descriptorsets(VKSwapchain::MAX_FRAMES_IN_FLIGHT * objects_.size()); 
        int descriptorSetIndex = 0;
        for (int frame = 0; frame < VKSwapchain::MAX_FRAMES_IN_FLIGHT; frame++) 
        {
            auto bufferInfo = ubobuffs[frame]->descriptorInfo();

            for (auto& obj : objects_)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = obj.model_->getimgview();
                imageInfo.sampler = obj.model_->getsampler();

                VKDescriptors::DescriptorWriter(*setlayout, *globalPool).writeBuffer(0, &bufferInfo).writeImage(1, &imageInfo).build(descriptorsets[descriptorSetIndex]);

                descriptorSetIndex++;  // Increment the index explicitly
            }
        }

        auto descriptorSetLayouts = std::vector<VkDescriptorSetLayout> {setlayout->getDescriptorSetLayout()};
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
            camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

            if (auto commandBuffer = renderer_.beginFrame())
            {
                int frameindex = renderer_.getframeindex();

                std::vector<VkDescriptorSet> Descriptorsets {};
                for (int i = 0, len = objects_.size(); i < objects_.size(); i++)
                    descriptorsets.push_back(descriptorsets[len * frameindex + i]);
                

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

        for (int i = 0; i < 10; i++)
        {
            for (int j = 0; j < 10; j++)
            {
                std::shared_ptr<VKModel::Model> model_viking_room =  VKModel::Model::createModelfromFile (device_,  "../../src/src/assets/viking_room.obj",
                                                                                                            "../../src/src/assets/viking_room.png");
                auto obj_viking_room                     =   VKObject::Object::createObject();
                obj_viking_room.model_                   =                  model_viking_room;
                obj_viking_room.transform3D_.translation =         {i * 2.0f, j * 2.0f, 0.0f};
                obj_viking_room.transform3D_.scale       =                    glm::vec3{0.8f};
                obj_viking_room.transform3D_.rotation    =               {1.57f, 1.57f, 0.0f};

                objects_.push_back(std::move(obj_viking_room));
            }
        }



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
