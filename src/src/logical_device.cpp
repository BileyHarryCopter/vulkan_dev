#include "device.hpp"

#include <iostream>
#include <cstring>

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

#ifdef USE_MESH_SHADING
        // First, check if the extension is supported
        bool meshShaderExtensionSupported = false;
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physdevice_, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(physdevice_, nullptr, &extensionCount, availableExtensions.data());
        
        for (const auto& ext : availableExtensions) {
            if (strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
                meshShaderExtensionSupported = true;
                break;
            }
        }

        if (!meshShaderExtensionSupported) {
            throw std::runtime_error("VK_EXT_mesh_shader extension not supported by physical device! "
                                     "Please use a GPU that supports VK_EXT_mesh_shader (NVIDIA RTX 20xx+, AMD RDNA2+, Intel Arc+).");
        }

        // Then, query if mesh shader features are supported
        // Use vkGetPhysicalDeviceFeatures2 to query extension features
        VkPhysicalDeviceMeshShaderFeaturesEXT queriedMeshShaderFeatures{};
        queriedMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        queriedMeshShaderFeatures.pNext = nullptr;

        VkPhysicalDeviceFeatures2 queriedFeatures2{};
        queriedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        queriedFeatures2.pNext = &queriedMeshShaderFeatures;
        queriedFeatures2.features = {}; // Initialize to zero

        // Query features - this should work even if extension is not enabled in device creation
        vkGetPhysicalDeviceFeatures2(physdevice_, &queriedFeatures2);
        
        // Also check driver version and Vulkan version
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physdevice_, &props);
        std::cout << "Vulkan API version: " << VK_VERSION_MAJOR(props.apiVersion) << "." 
                  << VK_VERSION_MINOR(props.apiVersion) << "." 
                  << VK_VERSION_PATCH(props.apiVersion) << std::endl;
        std::cout << "Driver version: " << VK_VERSION_MAJOR(props.driverVersion) << "." 
                  << VK_VERSION_MINOR(props.driverVersion) << "." 
                  << VK_VERSION_PATCH(props.driverVersion) << std::endl;

        std::cout << "Mesh shader feature query: meshShader=" << (queriedMeshShaderFeatures.meshShader ? "true" : "false")
                  << ", taskShader=" << (queriedMeshShaderFeatures.taskShader ? "true" : "false") << std::endl;

        if (!queriedMeshShaderFeatures.meshShader) {
            std::cerr << "WARNING: Mesh shader feature reports false, but extension is present!" << std::endl;
            std::cerr << "Device: " << props.deviceName << std::endl;
            std::cerr << "This may indicate:" << std::endl;
            std::cerr << "  1. Driver version is too old (update NVIDIA drivers to 570+)" << std::endl;
            std::cerr << "  2. Driver bug or misconfiguration" << std::endl;
            std::cerr << "  3. Vulkan API version too low (requires 1.3+)" << std::endl;
            std::cerr << "Attempting to enable mesh shader anyway..." << std::endl;
            std::cerr << "If device creation fails, try updating NVIDIA drivers or disable mesh shading." << std::endl;
            
            // For RTX 5070 Ti, we know it supports mesh shading, so try anyway
            // The device creation will fail if it's truly not supported
            // We'll set meshShader to true anyway and let device creation validate
        }

        // Now set up mesh shader features for device creation
        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
        meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        meshShaderFeatures.pNext = nullptr;
        meshShaderFeatures.taskShader = VK_FALSE;  // Task shader not used
        meshShaderFeatures.meshShader = VK_TRUE;   // Enable mesh shader
        meshShaderFeatures.multiviewMeshShader = VK_FALSE;
        meshShaderFeatures.primitiveFragmentShadingRateMeshShader = VK_FALSE;
        meshShaderFeatures.meshShaderQueries = VK_FALSE;

        // Chain mesh shader features to device features
        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.pNext = &meshShaderFeatures;
        deviceFeatures2.features = deviceFeatures;
#endif

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                   =                       VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount    =             static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos       =                                    queueCreateInfos.data();
#ifdef USE_MESH_SHADING
        createInfo.pNext = &deviceFeatures2;
        createInfo.pEnabledFeatures = nullptr;  // Using pNext chain instead
#else
        createInfo.pEnabledFeatures        =                                            &deviceFeatures;
        createInfo.pNext = nullptr;
#endif
        createInfo.enabledExtensionCount   =    static_cast<uint32_t>(deviceExtensions_.size());
        createInfo.ppEnabledExtensionNames =                           deviceExtensions_.data();


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
