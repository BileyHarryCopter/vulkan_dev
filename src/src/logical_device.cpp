#include "device.hpp"

namespace VKDevice
{
    void Device::createLogicalDevice(VKInstance::Instance& instance)
    {
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices_.get_graphics_value(),
                                                  indices_.get_present_value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex =                                queueFamily;
            queueCreateInfo.queueCount       =                                          1;
            queueCreateInfo.pQueuePriorities =                             &queuePriority;

            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures  deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                   =                       VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount    =             static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos       =                                    queueCreateInfos.data();
        createInfo.pEnabledFeatures        =                                            &deviceFeatures;
        createInfo.enabledExtensionCount   =    static_cast<uint32_t>(instance.get_extensions().size());
        createInfo.ppEnabledExtensionNames =                           instance.get_extensions().data();


        if (instance.enabledebug())
        {
            createInfo.enabledLayerCount   = static_cast<uint32_t>(VKInstance::validationLayers.size());
            createInfo.ppEnabledLayerNames =                        VKInstance::validationLayers.data();
        }
        else
            createInfo.enabledLayerCount   = 0;

        if (vkCreateDevice(physdevice_, &createInfo, nullptr, &logicdevice_) != VK_SUCCESS)
            throw std::runtime_error("failed to create logical device!");

        vkGetDeviceQueue(logicdevice_, indices_.get_graphics_value(), 0, &graphics_queue_);
        vkGetDeviceQueue(logicdevice_,  indices_.get_present_value(), 0,  &present_queue_);
    }
}   //  end of VKDevice namespace
