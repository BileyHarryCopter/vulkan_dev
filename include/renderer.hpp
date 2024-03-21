#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <memory>
#include <cassert>

#include "device.hpp"
#include "pipeline.hpp"
#include "swapchain.hpp"
#include "object.hpp"

namespace VKRenderer
{

class Renderer final
{

    VKWindow::Window&                          window_;
    VKDevice::Device&                          device_;
    std::unique_ptr<VKSwapchain::Swapchain> swapchain_;
    std::vector<VkCommandBuffer>        commandbuffer_;

    uint32_t                    currentImageIndex_ = 0;
    bool                       isFrameStarted_ = false;

public:

    Renderer (VKWindow::Window& window, VKDevice::Device& device);
    ~Renderer();

    //  functions for setting frames before drawing
    VkCommandBuffer beginFrame();
    void endFrame();
    bool isFrameInProgress() const { return isFrameStarted_; }

    //  function of connection with render system by command buffer
    VkCommandBuffer get_currentcmdbuffer() const 
    {
        assert (isFrameStarted_ && "Cannot get commandbuffer while the frame is not processing");

        return commandbuffer_[currentImageIndex_]; 
    }

    //  functions for setting renderpass
    void beginSwapchainRenderpass(VkCommandBuffer commandBuffer);
    void   endSwapchainRenderpass(VkCommandBuffer commandBuffer);
    VkRenderPass getSwapChainRenderPass() const { return swapchain_->get_renderpass(); }
    float getAspectRatio () const { return swapchain_->extentAspectRatio(); }
    uint32_t getframeindex() const { return currentImageIndex_; }

private:
    void createCommandBuffers();
    void recreateSwapChain();
    void freeCommandBuffers();
};

}   //  end of the VKRenderer namespace
