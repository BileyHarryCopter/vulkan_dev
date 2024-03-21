#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "window.hpp"
#include "instance.hpp"

namespace VKSurface
{

class Surface final
{
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkInstance  instance_ = VK_NULL_HANDLE;
public:
    Surface(VKInstance::Instance& instance, VKWindow::Window& window);
    ~Surface();

    VkSurfaceKHR get() const { return surface_; }
};

}   //  end of VKSurface namespace
