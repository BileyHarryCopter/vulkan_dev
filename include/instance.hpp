#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <set>
#include <string>
#include <vector>
#include <stdexcept>

#include "window.hpp"

namespace VKInstance
{

const std::vector<const char*> validationLayers {  "VK_LAYER_KHRONOS_validation"  };
const std::vector<const char*> deviceExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

class Instance final
{

    VkInstance            instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR           surface_ = VK_NULL_HANDLE;

    std::vector<const char *>      deviceExtensions_;
    bool              enablePresentationMode = false;
    bool                      enableSwapChain = true;

    void createInstance();
    void createSurface(VKWindow::Window& window);

public:

    Instance (VKWindow::Window& window);
    ~Instance();

    VkInstance                       get()                                { return instance_; }
    const std::vector<const char *>& get_extensions()  const      {  return deviceExtensions; }
    bool                             enabledebug()     const { return enableValidationLayers; }
    bool                             enablepresent()   const { return enablePresentationMode; }
    bool                             enableswapchain() const { return        enableSwapChain; }

    VkSurfaceKHR                     get_surface()        { return surface_; }
    // const VkSurfaceKHR               get_surface()  const { return surface_; }

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

private:

    VkDebugUtilsMessengerEXT  debugmsngr_ = nullptr;
    void setupDebugMessenger();
};

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);
std::vector<const char *> getRequiredExtensions(bool enableValidationLayers);
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator);
bool checkValidationLayerSupport(const std::vector<const char *> &valLayers);

}   //  end of VKInstance namespace
