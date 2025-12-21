#include "object.hpp"

namespace VKObject
{

    // Вспомогательная функция для вычисления матрицы модели
    static glm::mat4 computeModelMatrix(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
    {
        const float c3 = glm::cos(rotation.z);
        const float s3 = glm::sin(rotation.z);
        const float c2 = glm::cos(rotation.x);
        const float s2 = glm::sin(rotation.x);
        const float c1 = glm::cos(rotation.y);
        const float s1 = glm::sin(rotation.y);
        return glm::mat4{
            {
                scale.x * (c1 * c3 + s1 * s2 * s3),
                scale.x * (c2 * s3),
                scale.x * (c1 * s2 * s3 - c3 * s1),
                0.0f,
            },
            {
                scale.y * (c3 * s1 * s2 - c1 * s3),
                scale.y * (c2 * c3),
                scale.y * (c1 * c3 * s2 + s1 * s3),
                0.0f,
            },
            {
                scale.z * (c2 * s1),
                scale.z * (-s2),
                scale.z * (c1 * c2),
                0.0f,
            },
            {translation.x, translation.y, translation.z, 1.0f}};
    }

    // Вспомогательная функция для вычисления матрицы нормалей
    static glm::mat3 computeNormalMatrix(const glm::vec3& rotation, const glm::vec3& scale)
    {
        const float c3 = glm::cos(rotation.z);
        const float s3 = glm::sin(rotation.z);
        const float c2 = glm::cos(rotation.x);
        const float s2 = glm::sin(rotation.x);
        const float c1 = glm::cos(rotation.y);
        const float s1 = glm::sin(rotation.y);
        const glm::vec3 invScale = 1.0f / scale;

        return glm::mat3{
            {
                invScale.x * (c1 * c3 + s1 * s2 * s3),
                invScale.x * (c2 * s3),
                invScale.x * (c1 * s2 * s3 - c3 * s1),
            },
            {
                invScale.y * (c3 * s1 * s2 - c1 * s3),
                invScale.y * (c2 * c3),
                invScale.y * (c1 * c3 * s2 + s1 * s3),
            },
            {
                invScale.z * (c2 * s1),
                invScale.z * (-s2),
                invScale.z * (c1 * c2),
            },
        };
    }

    void Transform3Dcomponent::updateCache() const
    {
        cachedModelMatrix = computeModelMatrix(translation, rotation, scale);
        cachedNormalMatrix = computeNormalMatrix(rotation, scale);
        matrixCacheValid = true;
    }

    const glm::mat4& Transform3Dcomponent::getCachedModelMatrix() const
    {
        if (!matrixCacheValid) {
            updateCache();
        }
        return cachedModelMatrix;
    }

    const glm::mat3& Transform3Dcomponent::getCachedNormalMatrix() const
    {
        if (!matrixCacheValid) {
            updateCache();
        }
        return cachedNormalMatrix;
    }

    glm::mat4 Transform3Dcomponent::mat4() const
    {
        // Для статических объектов используем кэш
        if (isStatic && matrixCacheValid) {
            return cachedModelMatrix;
        }
        
        // Вычисляем матрицу
        glm::mat4 result = computeModelMatrix(translation, rotation, scale);
        
        // Кэшируем для статических объектов
        if (isStatic) {
            cachedModelMatrix = result;
            cachedNormalMatrix = computeNormalMatrix(rotation, scale);
            matrixCacheValid = true;
        }
        
        return result;
    }

    glm::mat3 Transform3Dcomponent::normalMatrix() const
    {
        // Для статических объектов используем кэш
        if (isStatic && matrixCacheValid) {
            return cachedNormalMatrix;
        }
        
        // Вычисляем матрицу
        glm::mat3 result = computeNormalMatrix(rotation, scale);
        
        // Кэшируем для статических объектов
        if (isStatic) {
            cachedNormalMatrix = result;
            // Также кэшируем modelMatrix, если еще не кэширована
            if (!matrixCacheValid) {
                cachedModelMatrix = computeModelMatrix(translation, rotation, scale);
                matrixCacheValid = true;
            }
        }
        
        return result;
    }

}   //  namespace of VKObject