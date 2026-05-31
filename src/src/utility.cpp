#include "utility.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

namespace Service
{

    std::vector<char> readfile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open())
            throw std::runtime_error("failed to open file!" + filename);

        size_t fileSize = (size_t) file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }

}      //  end of the Service namespace

namespace VKUtils
{
    SystemMemoryInfo getSystemMemoryInfo()
    {
        SystemMemoryInfo info = {0, 0, 0, false};
        
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo.is_open()) {
            return info;
        }
        
        uint64_t memTotal = 0;
        uint64_t memAvailable = 0;
        uint64_t memFree = 0;
        uint64_t buffers = 0;
        uint64_t cached = 0;
        
        std::string line;
        while (std::getline(meminfo, line)) {
            std::istringstream iss(line);
            std::string key;
            uint64_t value;
            std::string unit;
            
            iss >> key >> value >> unit;
            
            // Values in /proc/meminfo are in KB
            value *= 1024; // Convert to bytes
            
            if (key == "MemTotal:") {
                memTotal = value;
            } else if (key == "MemAvailable:") {
                memAvailable = value;
            } else if (key == "MemFree:") {
                memFree = value;
            } else if (key == "Buffers:") {
                buffers = value;
            } else if (key == "Cached:") {
                cached = value;
            }
        }
        
        if (memTotal > 0) {
            info.totalRam = memTotal;
            if (memAvailable > 0) {
                info.availableRam = memAvailable;
                info.usedRam = memTotal - memAvailable;
            } else {
                // Fallback: estimate available as free + buffers + cached
                info.availableRam = memFree + buffers + cached;
                info.usedRam = memTotal - info.availableRam;
            }
            info.isValid = true;
        }
        
        return info;
    }
}
