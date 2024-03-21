#pragma once

#include "model.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>

namespace VKObject
{

struct Transform3Dcomponent
{
    glm::vec3 translation{};    //  offest translation
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 rotation {};

    glm::mat4 mat4();
    glm::mat3 normalMatrix();

};

class Object
{

    using id_t = unsigned int;

    id_t id_;
    Object(id_t id) : id_{id} {}

public:
    static Object createObject ()
    {
        static id_t current_id = 0;
        return Object{current_id++};
    }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = default;
    Object& operator=(Object&&) = default;

    const id_t get_id() const { return id_; }

    std::shared_ptr<VKModel::Model> model_{};
    glm::vec3                       color_{};
    Transform3Dcomponent      transform3D_{};

};

}