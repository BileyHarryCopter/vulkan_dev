#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <functional>
#include <cstdint>

namespace Service
{

    std::vector<char> readfile(const std::string &filename);

    //  simple hash function
    template <typename T, typename... Rest>
    void hashCombine(std::size_t& seed, const T&v, const Rest&... rest)
    {
        seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        (hashCombine(seed, rest), ...);
    }

}   //  end of the Service namespace

namespace VKUtils
{
    struct SystemMemoryInfo {
        uint64_t totalRam;      // Total RAM in bytes
        uint64_t availableRam;  // Available RAM in bytes
        uint64_t usedRam;       // Used RAM in bytes
        bool isValid;           // Whether the info was successfully retrieved
    };
    
    SystemMemoryInfo getSystemMemoryInfo();
}
