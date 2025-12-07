#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace VKGpuMonitor
{

struct GpuStats {
    int utilizationPercent = -1;      // GPU utilization percentage (0-100), -1 if unavailable
    int temperatureCelsius = -1;       // GPU temperature in Celsius, -1 if unavailable
    uint64_t memoryUsedBytes = 0;      // VRAM used in bytes
    uint64_t memoryTotalBytes = 0;     // Total VRAM in bytes
    int clockGraphicsMhz = -1;         // Graphics clock in MHz, -1 if unavailable
    bool isValid = false;              // Whether the stats are valid
};

class GpuMonitor {
public:
    GpuMonitor();
    ~GpuMonitor() = default;

    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

    // Check if GPU monitoring is available (nvidia-smi exists)
    bool isAvailable() const { return isAvailable_; }

    // Query current GPU statistics
    GpuStats getCurrentStats();

    // Get GPU name/model
    std::string getGpuName() const { return gpuName_; }

private:
    bool isAvailable_ = false;
    std::string gpuName_;
    std::string nvidiaSmiPath_;

    bool detectNvidiaSmi();
    GpuStats parseNvidiaSmiOutput(const std::string& output);
    std::string executeCommand(const std::string& command);
};

}  // namespace VKGpuMonitor

