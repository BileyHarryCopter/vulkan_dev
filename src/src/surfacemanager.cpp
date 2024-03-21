#include "surfacemanager.hpp"

namespace VKSurface
{
    Surface::Surface(VKInstance::Instance &instance, VKWindow::Window &window) : instance_{instance.get()}
    {
        if (glfwCreateWindowSurface(instance.get(), window.get(), nullptr, &surface_) != VK_SUCCESS)
            throw std::runtime_error("failed to create window surface!");
    }

    Surface::~Surface()
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }

}   //  end of VKSurface namespace
