#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "instance.hpp"

#include <set>
#include <string>
#include <vector>
#include <optional>

namespace VKDevice
{

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t>  present;

    auto is_graphics() { return graphics.has_value(); }
    auto  is_present() { return present.has_value();  }

    auto get_graphics_value() { return graphics.value(); }
    auto  get_present_value() { return  present.value(); }
};

class Device final
{

    VkPhysicalDevice physdevice_ = VK_NULL_HANDLE;
    VkDevice        logicdevice_ = VK_NULL_HANDLE;

    QueueFamilyIndices                 indices_{};

    VkSurfaceKHR        surface_ = VK_NULL_HANDLE;

    VkQueue                       graphics_queue_;
    VkQueue                        present_queue_;
    VkCommandPool                    commandpool_;

    VkPhysicalDeviceProperties        properties_;
    const std::vector<const char *> deviceExtensions_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

public:

    Device (VKInstance::Instance& instance);

    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    Device(Device &&) = delete;
    Device& operator=(Device &&) = delete;

    ~Device();

    VkSurfaceKHR     get_surface() {   return surface_;  }

    VkPhysicalDevice get_phys   () { return physdevice_; }
    VkDevice         get_logic  () { return logicdevice_;}

    VkCommandPool    get_cmdpool() { return commandpool_;}

    bool is_graphics_complete() { return indices_.is_graphics(); }
    bool  is_present_complete() { return indices_.is_present();  }

    QueueFamilyIndices       &get_indices()       { return indices_; }
    const QueueFamilyIndices &get_indices() const { return indices_; }

    VkQueue get_graphics_queue() { return graphics_queue_; }
    VkQueue  get_present_queue() {  return present_queue_; }

    VkPhysicalDeviceProperties get_properties () const { return properties_;}

    void createBuffer(VkDeviceSize size,VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer &buffer, VkDeviceMemory &bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, 
                     VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    VkImageView createImageView(VkImage image, VkFormat format);

    uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

private:
    void pickPhysicalDevice (VKInstance::Instance& instance);
    void createLogicalDevice(VKInstance::Instance& instance);
    void createCommandPool  ();

    bool isDeviceSuitable(VkPhysicalDevice device, VKInstance::Instance &instance);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
};

}   //  end of VKDevice namespace
