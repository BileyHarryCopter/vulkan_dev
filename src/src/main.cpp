#include <iostream>
#include <cstdlib>

#include "app.hpp"

int main(int argv, char* argc[])
{
    VKEngine::App app{};

    try
    {
        app.run();
    }
    catch(const std::exception& exception)
    {
        std::cerr << exception.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
