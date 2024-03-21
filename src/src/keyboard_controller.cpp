#include "keyboard_controller.hpp"

// std
#include <limits>

namespace VKKeyboardController 
{
    void KeyboardController::moveInPlaneXZ(GLFWwindow* window, float dt, VKObject::Object& object) 
    {
        glm::vec3 rotate{0};
        if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) rotate.y += 10.5f;
        if (glfwGetKey(window, keys.lookLeft) == GLFW_PRESS) rotate.y -= 10.5f;
        if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS) rotate.x += 10.5f;
        if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS) rotate.x -= 10.5f;

        if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon())
            object.transform3D_.rotation += lookSpeed * dt * glm::normalize(rotate);

        // limit pitch values between about +/- 85ish degrees
        object.transform3D_.rotation.x = glm::clamp(object.transform3D_.rotation.x, -1.5f, 1.5f);
        object.transform3D_.rotation.y = glm::mod(object.transform3D_.rotation.y, glm::two_pi<float>());

        float yaw = object.transform3D_.rotation.y;
        const glm::vec3 forwardDir{sin(yaw), 0.f, cos(yaw)};
        const glm::vec3 rightDir{forwardDir.z, 0.f, -forwardDir.x};
        const glm::vec3 upDir{0.f, -1.f, 0.f};

        glm::vec3 moveDir{0.f};
        if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir += forwardDir;
        if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS) moveDir -= forwardDir;
        if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) moveDir += rightDir;
        if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) moveDir -= rightDir;
        if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) moveDir += upDir;
        if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) moveDir -= upDir;

        if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon())
            object.transform3D_.translation += moveSpeed * dt * glm::normalize(moveDir);
    }

}  // namespace lve