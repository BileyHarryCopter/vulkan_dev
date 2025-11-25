#include "instance.hpp"

#include "swapchain.hpp"

#include <cstring>
#include <iostream>
#include <algorithm>

namespace VKDevice
{
    void Device::createCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            =      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex =                   indices_.get_graphics_value();

        if (vkCreateCommandPool(logicdevice_, &poolInfo, nullptr, &commandpool_) != VK_SUCCESS)
            throw std::runtime_error("failed to create command pool!");
    }

    void findQueueFamilies(VkPhysicalDevice device, VKInstance::Instance& instance, 
                           QueueFamilyIndices& indices)
    {
        uint32_t queue_families_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_families(queue_families_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, queue_families.data());

        int index = 0;
        for (const auto& queue_family : queue_families)
        {
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                indices.graphics = index;

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, instance.get_surface(), &presentSupport);

            if (presentSupport)
                indices.present = index;

            if (indices.is_graphics() && indices.is_present())
                break;

            index++;
        }
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device, 
                                     const std::vector<const char *> deviceExtensions)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        for (const auto& extension : availableExtensions)
            requiredExtensions.erase(extension.extensionName);

        return requiredExtensions.empty();
    }

    bool Device::isDeviceSuitable(VkPhysicalDevice device, VKInstance::Instance& instance)
    {
        findQueueFamilies(device, instance, indices_);

        bool extensionsSupported = checkDeviceExtensionSupport(device, deviceExtensions_);

        if (instance.enableswapchain())
        {
            VKSwapchain::SwapChainSupportDetails swapChainSupport = VKSwapchain::querySwapChainSupport(device, instance.get_surface());
            bool swapChainAdequate = indices_.is_present() && !swapChainSupport.formats_.empty() && !swapChainSupport.presentModes_.empty();
        
            VkPhysicalDeviceFeatures supportedFeatures;
            vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

            return indices_.is_graphics() && indices_.is_present() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
        }

        return indices_.is_graphics() && extensionsSupported;
    }

    void Device::pickPhysicalDevice(VKInstance::Instance& instance)
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance.get(), &deviceCount, nullptr);
        
        if (deviceCount == 0)
            throw std::runtime_error("failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance.get(), &deviceCount, devices.data());

        // Collect all suitable devices and prioritize
        struct SuitableDevice {
            VkPhysicalDevice device;
            VkPhysicalDeviceProperties props;
            int priority; // Higher = better (NVIDIA discrete = 3, other discrete = 2, other = 1)
        };
        std::vector<SuitableDevice> suitableDevices;

        for (const auto& device : devices) 
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            
            if (isDeviceSuitable(device, instance))
            {
                SuitableDevice sd;
                sd.device = device;
                sd.props = props;
                
                // Calculate priority: NVIDIA discrete > other discrete > other
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    if (strstr(props.deviceName, "NVIDIA") != nullptr || 
                        strstr(props.deviceName, "RTX") != nullptr ||
                        strstr(props.deviceName, "GeForce") != nullptr) {
                        sd.priority = 3; // Highest priority
                    } else {
                        sd.priority = 2; // Discrete but not NVIDIA
                    }
                } else {
                    sd.priority = 1; // Other types
                }
                
                suitableDevices.push_back(sd);
            }   
        }

        if (suitableDevices.empty()) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }

        // Sort by priority (highest first)
        std::sort(suitableDevices.begin(), suitableDevices.end(), 
                  [](const SuitableDevice& a, const SuitableDevice& b) {
                      return a.priority > b.priority;
                  });

        // Select the best device
        physdevice_ = suitableDevices[0].device;
        properties_ = suitableDevices[0].props;

        // Log selected device
        std::cout << "Selected GPU: " << properties_.deviceName 
                  << " (Type: " << (properties_.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete" : "Other") << ")" << std::endl;

#ifdef USE_MESH_SHADING
        // Log available extensions for debugging
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physdevice_, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(physdevice_, nullptr, &extensionCount, availableExtensions.data());
        
        bool hasMeshShaderExt = false;
        for (const auto& ext : availableExtensions) {
            if (strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
                hasMeshShaderExt = true;
                std::cout << "Found VK_EXT_mesh_shader extension on selected device" << std::endl;
                break;
            }
        }
        if (!hasMeshShaderExt) {
            std::cout << "WARNING: VK_EXT_mesh_shader extension NOT found on selected device!" << std::endl;
        }
#endif
    }

}   //  end of VKInstance namespace
