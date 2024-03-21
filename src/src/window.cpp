#include "window.hpp"

namespace VKWindow
{
    
    void Window::initWindow()
    {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE,    GLFW_TRUE);

        window_ = glfwCreateWindow(width_, height_, window_name_.data(), nullptr, nullptr);
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, framebufferresizedCallback);
    }

    void Window::framebufferresizedCallback (window_ptr window, int width, int height)
    {
        auto new_window = reinterpret_cast<Window *> (glfwGetWindowUserPointer(window));
        new_window -> framebufferresized_ = true;
        new_window -> width_  =            width;
        new_window -> height_ =           height;
    }

}   //  end of VKWindow namespace
