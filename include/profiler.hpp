#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "device.hpp"
#include "swapchain.hpp"

#include <vector>
#include <cstdint>
#include <string>

namespace VKProfiler
{

enum class TimestampQuery {
    FRAME_START = 0,
    RENDERPASS_START,
    DRAW_START,
    DRAW_END,
    RENDERPASS_END,
    FRAME_END,
    COUNT  // Total number of timestamps per frame
};

struct ProfileResults {
    double frameTimeMs = 0.0;
    double renderPassTimeMs = 0.0;
    double drawTimeMs = 0.0;
    uint64_t frameStartTimestamp = 0;
    uint64_t frameEndTimestamp = 0;
    uint64_t renderPassStartTimestamp = 0;
    uint64_t renderPassEndTimestamp = 0;
    uint64_t drawStartTimestamp = 0;
    uint64_t drawEndTimestamp = 0;
    bool isValid = false;
};

class Profiler {
public:
    Profiler(VKDevice::Device& device);
    ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // Check if timestamp queries are supported
    bool isSupported() const { return isSupported_; }

    // Write a timestamp to the command buffer
    void writeTimestamp(VkCommandBuffer commandBuffer, uint32_t frameIndex, TimestampQuery query, VkPipelineStageFlagBits stage);

    // Reset query pool for a new frame
    void resetQueries(VkCommandBuffer commandBuffer, uint32_t frameIndex);

    // Get results for a specific frame (call after frame is complete)
    ProfileResults getResults(uint32_t frameIndex);

    // Convert timestamp difference to milliseconds
    double timestampToMs(uint64_t timestampDiff) const;

    // Get timestamp period (nanoseconds per timestamp increment)
    double getTimestampPeriod() const { return timestampPeriod_; }

private:
    VKDevice::Device& device_;
    bool isSupported_ = false;
    double timestampPeriod_ = 1.0;  // nanoseconds per timestamp increment
    
    // Query pools for each frame in flight
    std::vector<VkQueryPool> queryPools_;
    
    // Results storage (for reading back timestamps)
    std::vector<std::vector<uint64_t>> queryResults_;

    void createQueryPools();
    void destroyQueryPools();
};

}  // namespace VKProfiler

