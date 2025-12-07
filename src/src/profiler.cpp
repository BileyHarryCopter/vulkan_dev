#include "profiler.hpp"
#include "device.hpp"
#include "swapchain.hpp"

#include <stdexcept>
#include <iostream>

namespace VKProfiler
{

Profiler::Profiler(VKDevice::Device& device) : device_(device)
{
    // Check timestamp support
    VkPhysicalDeviceProperties properties = device_.get_properties();
    
    // Check if timestamps are supported
    // Most modern GPUs support timestamps, but we should check
    if (properties.limits.timestampComputeAndGraphics) {
        isSupported_ = true;
        timestampPeriod_ = properties.limits.timestampPeriod;  // nanoseconds per timestamp increment
        std::cout << "GPU Timestamp queries supported. Period: " << timestampPeriod_ << " ns" << std::endl;
    } else {
        isSupported_ = false;
        std::cerr << "WARNING: GPU does not support timestamp queries!" << std::endl;
        return;
    }

    createQueryPools();
}

Profiler::~Profiler()
{
    destroyQueryPools();
}

void Profiler::createQueryPools()
{
    if (!isSupported_) return;

    queryPools_.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);
    queryResults_.resize(VKSwapchain::MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < VKSwapchain::MAX_FRAMES_IN_FLIGHT; ++i) {
        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = static_cast<uint32_t>(TimestampQuery::COUNT);
        
        if (vkCreateQueryPool(device_.get_logic(), &poolInfo, nullptr, &queryPools_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create timestamp query pool!");
        }

        // Initialize results storage
        queryResults_[i].resize(static_cast<uint32_t>(TimestampQuery::COUNT), 0);
    }

    std::cout << "Created " << VKSwapchain::MAX_FRAMES_IN_FLIGHT 
              << " timestamp query pools with " << static_cast<uint32_t>(TimestampQuery::COUNT) 
              << " queries each" << std::endl;
}

void Profiler::destroyQueryPools()
{
    if (!isSupported_) return;

    for (auto& pool : queryPools_) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_.get_logic(), pool, nullptr);
            pool = VK_NULL_HANDLE;
        }
    }
    queryPools_.clear();
    queryResults_.clear();
}

void Profiler::resetQueries(VkCommandBuffer commandBuffer, uint32_t frameIndex)
{
    if (!isSupported_ || frameIndex >= queryPools_.size()) return;

    // Reset the query pool for this frame
    vkCmdResetQueryPool(commandBuffer, queryPools_[frameIndex], 0, static_cast<uint32_t>(TimestampQuery::COUNT));
}

void Profiler::writeTimestamp(VkCommandBuffer commandBuffer, uint32_t frameIndex, TimestampQuery query, VkPipelineStageFlagBits stage)
{
    if (!isSupported_ || frameIndex >= queryPools_.size()) return;

    uint32_t queryIndex = static_cast<uint32_t>(query);
    vkCmdWriteTimestamp(commandBuffer, stage, queryPools_[frameIndex], queryIndex);
}

ProfileResults Profiler::getResults(uint32_t frameIndex)
{
    ProfileResults results{};
    
    if (!isSupported_ || frameIndex >= queryPools_.size()) {
        results.isValid = false;
        return results;
    }

    // Read query results - we need to wait for the frame to complete first
    // This should be called after vkQueueWaitIdle or similar
    VkResult queryResult = vkGetQueryPoolResults(
        device_.get_logic(),
        queryPools_[frameIndex],
        0,
        static_cast<uint32_t>(TimestampQuery::COUNT),
        queryResults_[frameIndex].size() * sizeof(uint64_t),
        queryResults_[frameIndex].data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );

    if (queryResult != VK_SUCCESS) {
        results.isValid = false;
        return results;
    }

    // Extract timestamps
    results.frameStartTimestamp = queryResults_[frameIndex][static_cast<uint32_t>(TimestampQuery::FRAME_START)];
    results.frameEndTimestamp = queryResults_[frameIndex][static_cast<uint32_t>(TimestampQuery::FRAME_END)];
    results.renderPassStartTimestamp = queryResults_[frameIndex][static_cast<uint32_t>(TimestampQuery::RENDERPASS_START)];
    results.renderPassEndTimestamp = queryResults_[frameIndex][static_cast<uint32_t>(TimestampQuery::RENDERPASS_END)];
    results.drawStartTimestamp = queryResults_[frameIndex][static_cast<uint32_t>(TimestampQuery::DRAW_START)];
    results.drawEndTimestamp = queryResults_[frameIndex][static_cast<uint32_t>(TimestampQuery::DRAW_END)];

    // Calculate times in milliseconds
    if (results.frameEndTimestamp > results.frameStartTimestamp) {
        results.frameTimeMs = timestampToMs(results.frameEndTimestamp - results.frameStartTimestamp);
    }
    
    if (results.renderPassEndTimestamp > results.renderPassStartTimestamp) {
        results.renderPassTimeMs = timestampToMs(results.renderPassEndTimestamp - results.renderPassStartTimestamp);
    }
    
    if (results.drawEndTimestamp > results.drawStartTimestamp) {
        results.drawTimeMs = timestampToMs(results.drawEndTimestamp - results.drawStartTimestamp);
    }

    results.isValid = true;
    return results;
}

double Profiler::timestampToMs(uint64_t timestampDiff) const
{
    if (!isSupported_ || timestampPeriod_ <= 0.0) return 0.0;
    
    // timestampPeriod_ is in nanoseconds per increment
    // Convert to milliseconds: (timestampDiff * timestampPeriod_) / 1,000,000
    return (static_cast<double>(timestampDiff) * timestampPeriod_) / 1'000'000.0;
}

}  // namespace VKProfiler

