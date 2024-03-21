#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <functional>

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
