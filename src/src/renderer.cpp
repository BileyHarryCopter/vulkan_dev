#include "renderer.hpp"
#include "utility.hpp"
#include <iostream>
#include <sstream>

namespace VKRenderer
{
    Renderer::Renderer (VKWindow::Window& window, VKDevice::Device& device) : 
                        window_{window}, device_{device}
    {
        swapchain_ = std::make_unique<VKSwapchain::Swapchain>(window_, device_);
        recreateSwapChain();

        createCommandBuffers();
        
        // Initialize profiler
        try {
            profiler_ = std::make_unique<VKProfiler::Profiler>(device_);
            if (!profiler_->isSupported()) {
                std::cerr << "WARNING: GPU timestamp queries not supported. Profiling disabled." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "WARNING: Failed to initialize profiler: " << e.what() << std::endl;
        }
    }

    Renderer::~Renderer () { freeCommandBuffers(); }

    void Renderer::freeCommandBuffers()
    {
        vkFreeCommandBuffers(device_.get_logic(), device_.get_cmdpool(),
                             static_cast<uint32_t>(commandbuffer_.size()), commandbuffer_.data());
        commandbuffer_.clear();
    }

    void Renderer::recreateSwapChain()
    {
        try {
            auto extent = window_.get_extent();
            while (extent.width == 0 || extent.height == 0)
            {
                auto extent = window_.get_extent();
                glfwWaitEvents();
            }
            vkDeviceWaitIdle(device_.get_logic());

            if (swapchain_->get_swapchain() == nullptr)
                swapchain_ = std::make_unique<VKSwapchain::Swapchain>(window_, device_);
            else
                swapchain_ = std::make_unique<VKSwapchain::Swapchain>(window_, device_, std::move(swapchain_));
        } catch (const std::exception& e) {
            std::cerr << "\n=== ERROR DURING SWAPCHAIN RECREATION ===" << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
            
            // Print current memory state
            VKDevice::Device::MemoryInfo memInfo = device_.getMemoryInfo();
            VKUtils::SystemMemoryInfo sysMemInfo = VKUtils::getSystemMemoryInfo();
            
            std::cerr << "\nCurrent Memory State:" << std::endl;
            for (size_t i = 0; i < memInfo.heaps.size(); ++i) {
                const auto& heap = memInfo.heaps[i];
                std::cerr << "  Heap " << i << " (" 
                         << (heap.isDeviceLocal ? "VRAM" : "System RAM") << "):" << std::endl;
                std::cerr << "    Total: " << (heap.totalSize / (1024 * 1024)) << " MB ("
                         << (heap.totalSize / (1024ULL * 1024 * 1024)) << " GB)" << std::endl;
                std::cerr << "    Available: " << (heap.availableSize / (1024 * 1024)) << " MB" << std::endl;
            }
            
            if (sysMemInfo.isValid) {
                std::cerr << "  System RAM:" << std::endl;
                std::cerr << "    Total: " << (sysMemInfo.totalRam / (1024 * 1024)) << " MB ("
                         << (sysMemInfo.totalRam / (1024ULL * 1024 * 1024)) << " GB)" << std::endl;
                std::cerr << "    Available: " << (sysMemInfo.availableRam / (1024 * 1024)) << " MB" << std::endl;
                std::cerr << "    Used: " << (sysMemInfo.usedRam / (1024 * 1024)) << " MB" << std::endl;
            }
            std::cerr << "==========================================\n" << std::endl;
            
            // Re-throw to allow upper-level handling
            throw;
        }
    }

    void Renderer::createCommandBuffers()
    {
        commandbuffer_.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        =                          device_.get_cmdpool();
        allocInfo.level              =                VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount =   static_cast<uint32_t>(commandbuffer_.size());

        if (vkAllocateCommandBuffers(device_.get_logic(), &allocInfo, commandbuffer_.data()) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate command buffers!");
    }

    VkCommandBuffer Renderer::beginFrame()
    {
        assert(!isFrameStarted_ && "Can't call beginFrame while rendering is processing");
        auto result = swapchain_->acquireNextImage(&currentImageIndex_);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            try {
                recreateSwapChain();
            } catch (const std::exception&) {
                // Error already logged in recreateSwapChain, just return nullptr to skip frame
                return nullptr;
            }
            return nullptr;
        }
        // Handle VK_NOT_READY (image not available yet) - skip this frame
        if (result == VK_NOT_READY) {
            return nullptr;  // Skip frame, try again next iteration
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("failed to acquire swapchain image");

        isFrameStarted_ = true;

        auto commandBuffer = get_currentcmdbuffer();
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("failed to begin recording command buffer!");

        // Reset and write frame start timestamp
        if (profiler_ && profiler_->isSupported()) {
            profiler_->resetQueries(commandBuffer, currentImageIndex_);
            profiler_->writeTimestamp(commandBuffer, currentImageIndex_, 
                                     VKProfiler::TimestampQuery::FRAME_START, 
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }

        return commandBuffer;
    }

    void Renderer::endFrame()
    {
        assert(isFrameStarted_ && "Can't call endFrame while frame is not in progress");

        auto commandBuffer = get_currentcmdbuffer();
        
        // Write frame end timestamp before ending command buffer
        if (profiler_ && profiler_->isSupported()) {
            profiler_->writeTimestamp(commandBuffer, currentImageIndex_,
                                     VKProfiler::TimestampQuery::FRAME_END,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }
        
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("failed to record command buffer!");

        auto result = swapchain_->submitCommandBuffers(&commandBuffer, &currentImageIndex_);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_.wasresized())
        {
            window_.resetresized();
            try {
                recreateSwapChain();
            } catch (const std::exception&) {
                // Error already logged in recreateSwapChain
                // Don't throw here to allow graceful degradation
            }
        }
        else if (result != VK_SUCCESS)
            throw std::runtime_error("failed to present swapchain image");

        isFrameStarted_ = false;
        currentImageIndex_ = (currentImageIndex_ + 1) % VKSwapchain::MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::beginSwapchainRenderpass(VkCommandBuffer commandBuffer, bool useSecondaryCommandBuffers)
    {
        assert(isFrameStarted_ && "Can't call beginSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == get_currentcmdbuffer() && "Can't begining renderpass from different frames");
        
        auto swapchain_extent = swapchain_->get_extent();
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType             =         VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass        =                     swapchain_->get_renderpass();
        renderPassInfo.framebuffer       =  swapchain_->get_framebuffer(currentImageIndex_);
        renderPassInfo.renderArea.offset =                                           {0, 0};
        renderPassInfo.renderArea.extent =                                 swapchain_extent;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color        = {{0.1f, 0.15f, 0.2f, 1.0f}};
        clearValues[1].depthStencil =                     {1.0f, 0};

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues    =                        clearValues.data();

        // Write renderpass start timestamp
        if (profiler_ && profiler_->isSupported()) {
            profiler_->writeTimestamp(commandBuffer, currentImageIndex_,
                                     VKProfiler::TimestampQuery::RENDERPASS_START,
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }

        VkSubpassContents contents = useSecondaryCommandBuffers ? 
            VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, contents);

        VkViewport viewport{};
        viewport.x        =                            0.0f;
        viewport.y        =                            0.0f;
        viewport.width    = (float)  swapchain_extent.width;
        viewport.height   = (float) swapchain_extent.height;
        viewport.minDepth =                            0.0f;
        viewport.maxDepth =                            1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset =           {0, 0};
        scissor.extent = swapchain_extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endSwapchainRenderpass(VkCommandBuffer commandBuffer)
    {
        assert(isFrameStarted_ && "Can't call endSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == get_currentcmdbuffer() && "Can't end renderpass from different frames");

        // Write renderpass end timestamp before ending renderpass
        if (profiler_ && profiler_->isSupported()) {
            profiler_->writeTimestamp(commandBuffer, currentImageIndex_,
                                     VKProfiler::TimestampQuery::RENDERPASS_END,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }

        vkCmdEndRenderPass(commandBuffer);
    }

}
