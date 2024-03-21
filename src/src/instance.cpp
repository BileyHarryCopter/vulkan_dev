#include "instance.hpp"

namespace VKInstance
{

    Instance::Instance(VKWindow::Window& window)
    {
        createInstance();
        setupDebugMessenger();
        createSurface(window);
    }

    Instance::~Instance()
    {
        if (enableValidationLayers)
            DestroyDebugUtilsMessengerEXT(instance_, debugmsngr_, nullptr);

        if (enablePresentationMode)
            vkDestroySurfaceKHR(instance_, surface_, nullptr);

        vkDestroyInstance (instance_, nullptr);
    }

    void Instance::createInstance()
    {
        if (enableValidationLayers && !checkValidationLayerSupport(validationLayers))
            throw std::runtime_error("validation layers requested, but not available!");

        VkApplicationInfo appInfo{};
        appInfo.sType               = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName    =           "Triangles intersection";
        appInfo.applicationVersion  =           VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName         =                        "No Engine";
        appInfo.engineVersion       =           VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion          =                 VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   =         VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        =                                       &appInfo;
    
        auto extensions                    =  getRequiredExtensions(enableValidationLayers);
        createInfo.enabledExtensionCount   =       static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames =                              extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (enableValidationLayers) 
        {
            createInfo.enabledLayerCount   =         static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames =                                validationLayers.data();
            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext               = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
        } 
        else 
        {
            createInfo.enabledLayerCount   =       0;
            createInfo.pNext               = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS)
            throw std::runtime_error("failed to create instance");
    }

    void Instance::createSurface(VKWindow::Window& window)
    {
        if (glfwCreateWindowSurface(instance_, window.get(), nullptr, &surface_) != VK_SUCCESS)
            throw std::runtime_error("failed to create window surface!");
        enablePresentationMode = true;
    }

}   //  end of VKDevice namespace
