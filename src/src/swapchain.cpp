#include "swapchain.hpp"

#include "model.hpp"

#include <limits>
#include <algorithm>

namespace VKSwapchain
{

    Swapchain::Swapchain(VKWindow::Window& window, VKDevice::Device& device) : 
                         device_{device}, surface_{device.get_surface()}
    {
        createSwapChain(window);
        createImageViews();
        createRenderPass();
        createDepthResources();
        createFramebuffers();
        createSyncObjects();
    }

    Swapchain::Swapchain(VKWindow::Window& window, VKDevice::Device& device,
                         std::shared_ptr<Swapchain> previous) : 
                         device_{device}, surface_{device.get_surface()}, oldswapchain_{previous}
    {
        createSwapChain(window);
        createImageViews();
        createRenderPass();
        createDepthResources();
        createFramebuffers();
        createSyncObjects();

        oldswapchain_ = nullptr;    //  because of previous is shared_ptr
    }

    Swapchain::~Swapchain()
    {
        for (auto imageview : swapchainimageviews_)
            vkDestroyImageView(device_.get_logic(), imageview, nullptr);
        swapchainimageviews_.clear();

        if (swapchain_ != nullptr)
        {
            vkDestroySwapchainKHR(device_.get_logic(), swapchain_, nullptr);
            swapchain_ = nullptr;
        }

        for (int i = 0; i < depthimages_.size(); i++)
        {
            vkDestroyImageView(device_.get_logic(),   depthimageviews_[i], nullptr);
            vkDestroyImage    (device_.get_logic(),       depthimages_[i], nullptr);
            vkFreeMemory      (device_.get_logic(), depthimagememorys_[i], nullptr);
        }

        for (auto framebuffer : swapchainframebuffers_)
            vkDestroyFramebuffer(device_.get_logic(), framebuffer, nullptr);

        vkDestroyRenderPass(device_.get_logic(), renderpass_, nullptr);

        for (auto i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroySemaphore(device_.get_logic(), renderfinishedsemaphore_[i], nullptr);
            vkDestroySemaphore(device_.get_logic(), imageavailablesemaphore_[i], nullptr);
            vkDestroyFence    (device_.get_logic(),           inflightfence_[i], nullptr);
        }
    }

    VkResult Swapchain::acquireNextImage(uint32_t *imageIndex) 
    { 
        vkWaitForFences(device_.get_logic(), 1, &inflightfence_[currentframe_], VK_TRUE, std::numeric_limits<uint64_t>::max());

        auto result = vkAcquireNextImageKHR(device_.get_logic(), swapchain_, std::numeric_limits<uint64_t>::max(), 
                                            imageavailablesemaphore_[currentframe_], VK_NULL_HANDLE, imageIndex);

        if (*imageIndex >= MAX_FRAMES_IN_FLIGHT)
            result = VK_ERROR_OUT_OF_DATE_KHR;

        return result;
    }

    VkResult Swapchain::submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex)
    {
        if (imagesinflight_[*imageIndex] != VK_NULL_HANDLE)
            vkWaitForFences(device_.get_logic(), 1, &imagesinflight_[*imageIndex], VK_TRUE, UINT64_MAX);
        imagesinflight_[*imageIndex] = inflightfence_[currentframe_];

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore      waitSemaphores[] =            {imageavailablesemaphore_[currentframe_]};

        VkPipelineStageFlags waitStages[] =      {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount     =                                                    1;
        submitInfo.pWaitSemaphores        =                                       waitSemaphores;
        submitInfo.pWaitDstStageMask      =                                           waitStages;
        submitInfo.commandBufferCount     =                                                    1;
        submitInfo.pCommandBuffers        =                                              buffers;

        VkSemaphore signalSemaphores[]    =            {renderfinishedsemaphore_[currentframe_]};
        submitInfo.signalSemaphoreCount   =                                                    1;
        submitInfo.pSignalSemaphores      =                                     signalSemaphores;

        vkResetFences  (device_.get_logic(), 1, &inflightfence_[currentframe_]);

        auto result = vkQueueSubmit(device_.get_graphics_queue(), 1, &submitInfo, inflightfence_[currentframe_]);
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to submit draw command buffer!");
        
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount =                                  1;
        presentInfo.pWaitSemaphores    =                   signalSemaphores;

        VkSwapchainKHR swapChains[] =                  {swapchain_};
        presentInfo.swapchainCount  =                             1;
        presentInfo.pSwapchains     =                    swapChains;
        presentInfo.pImageIndices   =                    imageIndex;

        vkQueuePresentKHR(device_.get_present_queue(), &presentInfo);

        currentframe_update();

        return result;
    }

    void Swapchain::createSyncObjects() 
    {
        renderfinishedsemaphore_.resize(MAX_FRAMES_IN_FLIGHT);
        imageavailablesemaphore_.resize(MAX_FRAMES_IN_FLIGHT);
                  inflightfence_.resize(MAX_FRAMES_IN_FLIGHT);
         imagesinflight_.resize(imageCount(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType     =     VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags     =            VK_FENCE_CREATE_SIGNALED_BIT;

        for (auto i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (vkCreateSemaphore(device_.get_logic(), &semaphoreInfo, nullptr, &imageavailablesemaphore_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device_.get_logic(), &semaphoreInfo, nullptr, &renderfinishedsemaphore_[i]) != VK_SUCCESS ||
                vkCreateFence    (device_.get_logic(),     &fenceInfo, nullptr,           &inflightfence_[i]) != VK_SUCCESS)
                throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) 
    {
        for (const auto& availableFormat : availableFormats)
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return availableFormat;

        return availableFormats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) 
    {
        for (const auto& availablePresentMode : availablePresentModes)
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
                return availablePresentMode;

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                VKWindow::Window& window) 
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return capabilities.currentExtent;
        else 
        {
            int width, height;
            glfwGetFramebufferSize(window.get(), &width, &height);

            VkExtent2D actualExtent = 
            {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width  = std::clamp(actualExtent.width,
                                             capabilities.minImageExtent.width,
                                             capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height,
                                             capabilities.minImageExtent.height,
                                             capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void Swapchain::createSwapChain(VKWindow::Window& window)
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device_.get_phys(), surface_);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat   (swapChainSupport.formats_);
        VkPresentModeKHR   presentMode   = chooseSwapPresentMode(swapChainSupport.presentModes_);
        VkExtent2D         extent        = chooseSwapExtent      (swapChainSupport.capabilities_, 
                                                                                         window);

        uint32_t imageCount              =      swapChainSupport.capabilities_.minImageCount + 1;
        if (swapChainSupport.capabilities_.maxImageCount > 0 && 
            imageCount > swapChainSupport.capabilities_.maxImageCount)
            imageCount = swapChainSupport.capabilities_.maxImageCount;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface =                                    surface_;

        createInfo.minImageCount    =                          imageCount;
        createInfo.imageFormat      =                surfaceFormat.format;
        createInfo.imageColorSpace  =            surfaceFormat.colorSpace;
        createInfo.imageExtent      =                              extent;
        createInfo.imageArrayLayers =                                   1;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto indices = device_.get_indices();
        uint32_t queueFamilyIndices[] = {indices.get_graphics_value(),
                                         indices.get_present_value()};

        if (indices.get_graphics_value() != indices.get_present_value())
        {
            createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount =                          2;
            createInfo.pQueueFamilyIndices   =         queueFamilyIndices;
        } 
        else
        {
            createInfo.imageSharingMode      =  VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount =                          0;        
            createInfo.pQueueFamilyIndices   =                    nullptr;  
        }

        createInfo.preTransform   =                            swapChainSupport.capabilities_.currentTransform;
        createInfo.compositeAlpha =                                          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode    =                                                                presentMode;
        createInfo.clipped        =                                                                    VK_TRUE;
        createInfo.oldSwapchain   = oldswapchain_ == nullptr ? VK_NULL_HANDLE : oldswapchain_->get_swapchain();

        if (vkCreateSwapchainKHR(device_.get_logic(), &createInfo, nullptr, &swapchain_) != VK_SUCCESS)
            throw std::runtime_error("failed to create swap chain!");

        vkGetSwapchainImagesKHR(device_.get_logic(), swapchain_, &imageCount, nullptr);
        swapchainimages_.resize (imageCount);
        vkGetSwapchainImagesKHR(device_.get_logic(), swapchain_, &imageCount, swapchainimages_.data());

        swapchainimageformat_ = surfaceFormat.format;
        swapchainextent_      =               extent;
    }

    void Swapchain::createImageViews()
    {
        swapchainimageviews_.resize(swapchainimages_.size());

        for (size_t i = 0; i < swapchainimages_.size(); i++) 
            swapchainimageviews_[i] = device_.createImageView(swapchainimages_[i], swapchainimageformat_);
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities_);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            details.formats_.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats_.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) 
        {
            details.presentModes_.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes_.data());
        }

        return details;
    }

    void Swapchain::createImageWithInfo(const VkImageCreateInfo &imageInfo, VkMemoryPropertyFlags properties,
                                        VkImage &image, VkDeviceMemory &imageMemory)
    {
        if (vkCreateImage(device_.get_logic(), &imageInfo, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("failed to create image!");

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device_.get_logic(), image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = device_.findMemoryType(device_.get_phys(), memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device_.get_logic(), &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate image memory!");

        if (vkBindImageMemory(device_.get_logic(), image, imageMemory, 0) != VK_SUCCESS)
            throw std::runtime_error("failed to bind image memory!");
    }

    VkFormat findSupportedFormat(VkPhysicalDevice phys_device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
    {
        for (VkFormat format : candidates) 
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(phys_device, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) 
                return format; 
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
                return format;
        }

        throw std::runtime_error("failed to find supported format!");
    }

    VkFormat findDepthFormat(VkPhysicalDevice device) 
    {
        return findSupportedFormat(device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                    VK_IMAGE_TILING_OPTIMAL,         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }
    bool hasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void Swapchain::createDepthResources()
    {
        VkFormat depthFormat  = findDepthFormat(device_.get_phys());
        swapchaindepthformat_ =       depthFormat;
        VkExtent2D swapChainExtent = get_extent();

        depthimages_.resize(imageCount());
        depthimagememorys_.resize(imageCount());
        depthimageviews_.resize(imageCount());

        for (int i = 0; i < depthimages_.size(); i++) 
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType         =   VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     =                      VK_IMAGE_TYPE_2D;
            imageInfo.extent.width  =                 swapChainExtent.width;
            imageInfo.extent.height =                swapChainExtent.height;
            imageInfo.extent.depth  =                                     1;
            imageInfo.mipLevels     =                                     1;
            imageInfo.arrayLayers   =                                     1;
            imageInfo.format        =                           depthFormat;
            imageInfo.tiling        =               VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout =             VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage =   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples       =                 VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode   =             VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags         =                                     0;

            createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthimages_[i], depthimagememorys_[i]);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType     =    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image     =                             depthimages_[i];
            viewInfo.viewType  =                       VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format    =                                 depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel   =                     0;
            viewInfo.subresourceRange.levelCount     =                     1;
            viewInfo.subresourceRange.baseArrayLayer =                     0;
            viewInfo.subresourceRange.layerCount     =                     1;

            if (vkCreateImageView(device_.get_logic(), &viewInfo, nullptr, &depthimageviews_[i]) != VK_SUCCESS)
                throw std::runtime_error("failed to create texture image view!");
        }
    }

}   //  end of VKSwapchain namespace
