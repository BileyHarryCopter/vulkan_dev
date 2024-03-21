#include "instance.hpp"

#include "swapchain.hpp"

#include <cstring>

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

        for (const auto& device : devices) 
        {
            if (isDeviceSuitable(device, instance))
            {
                physdevice_ = device;
                break;
            }   
        }

        if (physdevice_ == VK_NULL_HANDLE)
            throw std::runtime_error("failed to find a suitable GPU!");

        // vkGetPhysicalDeviceProperties(physdevice_, &properties);  -  why does it not work?
    }

}   //  end of VKInstance namespace
