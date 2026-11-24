#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace VKEngine
{

/** Returns true if the world-space AABB intersects the view frustum (i.e. might be visible). */
inline bool intersectFrustum(const glm::mat4& viewProj, const glm::vec3& worldMin, const glm::vec3& worldMax)
{
    const glm::vec4 corners[8] = {
        viewProj * glm::vec4(worldMin.x, worldMin.y, worldMin.z, 1.f),
        viewProj * glm::vec4(worldMax.x, worldMin.y, worldMin.z, 1.f),
        viewProj * glm::vec4(worldMin.x, worldMax.y, worldMin.z, 1.f),
        viewProj * glm::vec4(worldMax.x, worldMax.y, worldMin.z, 1.f),
        viewProj * glm::vec4(worldMin.x, worldMin.y, worldMax.z, 1.f),
        viewProj * glm::vec4(worldMax.x, worldMin.y, worldMax.z, 1.f),
        viewProj * glm::vec4(worldMin.x, worldMax.y, worldMax.z, 1.f),
        viewProj * glm::vec4(worldMax.x, worldMax.y, worldMax.z, 1.f),
    };

    float maxXPlusW = -1e30f, minXMinusW = 1e30f;
    float maxYPlusW = -1e30f, minYMinusW = 1e30f;
    float maxZ = -1e30f, minZMinusW = 1e30f;
    int behind = 0;

    for (int i = 0; i < 8; i++)
    {
        const float w = corners[i].w;
        if (w <= 0.f) { behind++; continue; }
        const float x = corners[i].x, y = corners[i].y, z = corners[i].z;
        maxXPlusW = glm::max(maxXPlusW, x + w);
        minXMinusW = glm::min(minXMinusW, x - w);
        maxYPlusW = glm::max(maxYPlusW, y + w);
        minYMinusW = glm::min(minYMinusW, y - w);
        maxZ = glm::max(maxZ, z);
        minZMinusW = glm::min(minZMinusW, z - w);
    }

    if (behind == 8) return false;
    if (maxXPlusW < 0.f) return false;   // left
    if (minXMinusW > 0.f) return false;  // right
    if (maxYPlusW < 0.f) return false;   // bottom
    if (minYMinusW > 0.f) return false;  // top
    if (maxZ < 0.f) return false;       // near
    if (minZMinusW > 0.f) return false; // far
    return true;
}

/** Transform AABB from model space to world space (min/max of 8 transformed corners). */
inline void transformAabb(const glm::mat4& model, const glm::vec3& modelMin, const glm::vec3& modelMax,
                         glm::vec3& worldMin, glm::vec3& worldMax)
{
    const glm::vec3 corners[8] = {
        glm::vec3(modelMin.x, modelMin.y, modelMin.z),
        glm::vec3(modelMax.x, modelMin.y, modelMin.z),
        glm::vec3(modelMin.x, modelMax.y, modelMin.z),
        glm::vec3(modelMax.x, modelMax.y, modelMin.z),
        glm::vec3(modelMin.x, modelMin.y, modelMax.z),
        glm::vec3(modelMax.x, modelMin.y, modelMax.z),
        glm::vec3(modelMin.x, modelMax.y, modelMax.z),
        glm::vec3(modelMax.x, modelMax.y, modelMax.z),
    };
    glm::vec3 p0 = glm::vec3(model * glm::vec4(corners[0], 1.f));
    worldMin = worldMax = p0;
    for (int i = 1; i < 8; i++)
    {
        glm::vec3 p = glm::vec3(model * glm::vec4(corners[i], 1.f));
        worldMin = glm::min(worldMin, p);
        worldMax = glm::max(worldMax, p);
    }
}

}  // namespace VKEngine
