#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>

namespace VKWindow
{

constexpr std::size_t DEFAULT_WIDTH         = 1400;
constexpr std::size_t DEFAULT_HEIGHT        =  900;
const     std::string DEFAULT_WINDOW_NAME   = "VK";

class Window final
{

    using size_t     =    std::size_t;
    using str_t      =    std::string;
    using window_ptr =    GLFWwindow*;

    window_ptr                window_;
    str_t                window_name_;
    size_t width_    =              0,
           height_   =              0;
    bool framebufferresized_  = false;  //  flag for resizing he window

    void initWindow();
    static void framebufferresizedCallback (window_ptr window, int width, int height);

public:

    Window (const size_t& width, const size_t height, const str_t& name) :
                              width_{width}, height_{height}, window_name_{name}
    {
        initWindow();
    }
    ~Window()
    {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
    //  To avoid double call of glfwDestroyWindow()
    Window (const Window& window) = delete;
    Window &operator=(const Window &window) = delete;

    bool shouldClose()           { return glfwWindowShouldClose(window_); }
    bool wasresized()                       { return framebufferresized_; }
    void resetresized()                    { framebufferresized_ = false; }

    window_ptr get()                                    { return window_; }
    VkExtent2D get_extent() {return {(uint32_t)width_, (uint32_t)height_};}
};

}   //  end of VKWindow class
