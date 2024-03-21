#pragma once

#include <iostream>
#include <chrono>

#include "window.hpp"
#include "instance.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "object.hpp"
#include "camera.hpp"
#include "keyboard_controller.hpp"
#include "buffmanager.hpp"
#include "descriptors.hpp"

namespace VKEngine
{

struct GlobalUbo
{
    glm::mat4 projectionView {1.f};
    glm::vec3 lightDirection = glm::normalize(glm::vec3{2.0, 3.0, 1.0});
};

class App final
{
    VKWindow::Window               window_;
    VKInstance::Instance         instance_;
    VKDevice::Device               device_;
    VKRenderer::Renderer         renderer_;

    //  oreder matters
    std::unique_ptr<VKDescriptors::DescriptorPool> globalPool {};
    std::vector<VKObject::Object> objects_;

public:
    App() : 
        window_{VKWindow::DEFAULT_WIDTH, 
                VKWindow::DEFAULT_HEIGHT, 
                VKWindow::DEFAULT_WINDOW_NAME},
        instance_{window_}, device_{instance_}, renderer_ {window_, device_}
    {

        globalPool = VKDescriptors::DescriptorPool::Builder(device_).setMaxSets(VKSwapchain::MAX_FRAMES_IN_FLIGHT)
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKSwapchain::MAX_FRAMES_IN_FLIGHT)
                                                                    .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VKSwapchain::MAX_FRAMES_IN_FLIGHT).build();
        loadObjects();
    }
    ~App()= default;

    void run();

private:
    void loadObjects();
    
};

}   //  end of VKEngine class
