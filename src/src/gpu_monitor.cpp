#include "gpu_monitor.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <regex>
#include <vector>
#include <cstdio>

namespace VKGpuMonitor
{

GpuMonitor::GpuMonitor()
{
    isAvailable_ = detectNvidiaSmi();
    if (isAvailable_) {
        // Get GPU name
        std::string nameOutput = executeCommand(nvidiaSmiPath_ + " --query-gpu=name --format=csv,noheader,nounits");
        if (!nameOutput.empty()) {
            // Remove trailing newline
            gpuName_ = nameOutput;
            gpuName_.erase(std::remove(gpuName_.begin(), gpuName_.end(), '\n'), gpuName_.end());
            gpuName_.erase(std::remove(gpuName_.begin(), gpuName_.end(), '\r'), gpuName_.end());
        }
    }
}

bool GpuMonitor::detectNvidiaSmi()
{
    // Try common paths for nvidia-smi
    std::vector<std::string> possiblePaths = {
        "/usr/bin/nvidia-smi",
        "/usr/local/bin/nvidia-smi",
        "nvidia-smi"  // Try PATH
    };
    
    for (const auto& path : possiblePaths) {
        std::string testCommand = path + " --version > /dev/null 2>&1";
        if (system(testCommand.c_str()) == 0) {
            nvidiaSmiPath_ = path;
            return true;
        }
    }
    
    return false;
}

std::string GpuMonitor::executeCommand(const std::string& command)
{
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    pclose(pipe);
    return result;
}

GpuStats GpuMonitor::getCurrentStats()
{
    GpuStats stats;
    
    if (!isAvailable_) {
        stats.isValid = false;
        return stats;
    }
    
    // Query GPU stats using nvidia-smi
    std::string command = nvidiaSmiPath_ + 
        " --query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total,clocks.current.graphics"
        " --format=csv,noheader,nounits 2>/dev/null";
    
    std::string output = executeCommand(command);
    
    if (output.empty()) {
        stats.isValid = false;
        return stats;
    }
    
    return parseNvidiaSmiOutput(output);
}

GpuStats GpuMonitor::parseNvidiaSmiOutput(const std::string& output)
{
    GpuStats stats;
    
    std::istringstream iss(output);
    std::string line;
    if (!std::getline(iss, line)) {
        stats.isValid = false;
        return stats;
    }
    
    // Parse CSV format: utilization.gpu, temperature.gpu, memory.used, memory.total, clocks.current.graphics
    // Example: "95, 72, 8192, 16384, 2100"
    
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);
        tokens.push_back(token);
    }
    
    if (tokens.size() >= 5) {
        try {
            // Utilization (percentage)
            if (!tokens[0].empty()) {
                stats.utilizationPercent = std::stoi(tokens[0]);
            }
            
            // Temperature (Celsius)
            if (!tokens[1].empty()) {
                stats.temperatureCelsius = std::stoi(tokens[1]);
            }
            
            // Memory used (MiB, convert to bytes)
            if (!tokens[2].empty()) {
                uint64_t memoryUsedMiB = std::stoull(tokens[2]);
                stats.memoryUsedBytes = memoryUsedMiB * 1024ULL * 1024ULL;
            }
            
            // Memory total (MiB, convert to bytes)
            if (!tokens[3].empty()) {
                uint64_t memoryTotalMiB = std::stoull(tokens[3]);
                stats.memoryTotalBytes = memoryTotalMiB * 1024ULL * 1024ULL;
            }
            
            // Graphics clock (MHz)
            if (!tokens[4].empty()) {
                stats.clockGraphicsMhz = std::stoi(tokens[4]);
            }
            
            stats.isValid = true;
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to parse nvidia-smi output: " << e.what() << std::endl;
            stats.isValid = false;
        }
    } else {
        stats.isValid = false;
    }
    
    return stats;
}

}  // namespace VKGpuMonitor

