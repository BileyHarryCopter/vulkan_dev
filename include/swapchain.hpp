#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <vector>
#include <memory>

#include "device.hpp"

namespace VKSwapchain
{

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR                capabilities_;
    std::vector<VkSurfaceFormatKHR>              formats_;
    std::vector<VkPresentModeKHR>           presentModes_;
};

class Swapchain final
{
    VkSurfaceKHR                                 surface_;
    VKDevice::Device&                             device_;
    
    VkSwapchainKHR                             swapchain_;
    std::shared_ptr<Swapchain>              oldswapchain_;  //  for swapchain recreation

    VkRenderPass                              renderpass_;
    
    std::vector<VkImage>                 swapchainimages_;
    std::vector<VkImageView>         swapchainimageviews_;

    std::vector<VkImage>                     depthimages_;
    std::vector<VkDeviceMemory>        depthimagememorys_;
    std::vector<VkImageView>             depthimageviews_;

    VkFormat                        swapchainimageformat_;
    VkFormat                        swapchaindepthformat_;
    VkExtent2D                           swapchainextent_;

    std::vector<VkFramebuffer>     swapchainframebuffers_;

    std::vector<VkSemaphore>     imageavailablesemaphore_;
    std::vector<VkSemaphore>     renderfinishedsemaphore_;
    std::vector<VkFence>                   inflightfence_;
    std::vector<VkFence>                  imagesinflight_;
    std::size_t                         currentframe_ = 0;

public:

    Swapchain (VKWindow::Window& window, VKDevice::Device& device);
    Swapchain (VKWindow::Window& window, VKDevice::Device& device, std::shared_ptr<Swapchain> previous);

    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    VkResult acquireNextImage(uint32_t *imageIndex);
    VkResult submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex);

    size_t imageCount() { return swapchainimages_.size(); }

    //  functions of connection
    VkSwapchainKHR        get_swapchain()       { return swapchain_; }
    const VkSwapchainKHR& get_swapchain() const { return swapchain_; }

    VkRenderPass         get_renderpass()       {    return renderpass_;   }
    const VkRenderPass  &get_renderpass() const {    return renderpass_;   }

    const VkFramebuffer &get_framebuffer(std::size_t index) const { return swapchainframebuffers_[index]; }
    const VkExtent2D    &get_extent()     const { return swapchainextent_; }

    VkSemaphore       &get_available_img_semaphore()        { return imageavailablesemaphore_[currentframe_]; }
    const VkSemaphore &get_available_img_semaphore()  const { return imageavailablesemaphore_[currentframe_]; }

    VkSemaphore       &get_finished_rndr_semaphore()        { return renderfinishedsemaphore_[currentframe_]; }
    const VkSemaphore &get_finished_rndr_semaphore()  const { return renderfinishedsemaphore_[currentframe_]; }

    VkFence           &get_fence()        { return inflightfence_[currentframe_]; }
    const VkFence     &get_fence()  const { return inflightfence_[currentframe_]; }

    std::size_t get_index_currentframe() { return currentframe_; }
    void        currentframe_update()    { currentframe_ = (currentframe_ + 1) % MAX_FRAMES_IN_FLIGHT;}

    float extentAspectRatio() { return static_cast<float>(swapchainextent_.width) / static_cast<float>(swapchainextent_.height); }

private:
    void createSwapChain(VKWindow::Window& window);
    void createImageViews();
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void createSyncObjects();

    void createImageWithInfo (const VkImageCreateInfo &imageInfo, VkMemoryPropertyFlags properties,
                                    VkImage &image, VkDeviceMemory &imageMemory);
};

VkFormat findDepthFormat(VkPhysicalDevice device);
SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

}   //  end of VKSwapchain namespace
