#pragma once

#include <memory>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "device.hpp"
#include "buffmanager.hpp"


namespace VKModel
{

class Model final
{
    VKDevice::Device&                               device_;

    std::unique_ptr<VKBuffmanager::Buffmanager> vertexbuff_;
    uint32_t vertexcount_ = 0;

    bool hasindexbuffer = false; 
    std::unique_ptr<VKBuffmanager::Buffmanager>  indexbuff_;
    uint32_t indexcount_ = 0;

    VkImage             textureimg_ = VK_NULL_HANDLE;
    VkDeviceMemory   textureimgmem_ = VK_NULL_HANDLE;
    uint32_t       textureimgcount_ =              0;

    VkImageView     textureimgview_ = VK_NULL_HANDLE;
    VkSampler       texturesampler_ = VK_NULL_HANDLE;

public:

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3    color;
        glm::vec3   normal;
        glm::vec2       uv;
        int       texIndex;

        static std::vector<VkVertexInputBindingDescription>     get_binding_descriptions();
        static std::vector<VkVertexInputAttributeDescription> get_attribute_descriptions();

        bool operator== (const Vertex& rhs) const 
        {
            return position == rhs.position && color == rhs.color && normal == rhs.normal && uv == rhs.uv && texIndex == rhs.texIndex;
        }
    };

    struct Builder 
    {
        std::vector<Vertex>   vertices{};
        std::vector<uint32_t>  indices{};

        std::string  filepath_to_texture;

        void load_models (const std::string& filepath_to_model);
    };

    Model (VKDevice::Device& device, const VKModel::Model::Builder& builder);
    ~Model();

    Model(const Model &rhs) = delete;
    Model &operator=(const Model& rhs) = delete;

    //  function for building a model from obj file and texture
    static std::unique_ptr<Model> createModelfromFile (VKDevice::Device& device,const std::string& filepath_to_model, 
                                                                                const std::string& filepath_to_texture);

    void bind(VkCommandBuffer commandbuffer);
    void draw(VkCommandBuffer commandbuffer);

    VkImageView getimgview() { return textureimgview_; }
    VkSampler   getsampler() { return texturesampler_; }
    bool has_texture() { return textureimg_ != VK_NULL_HANDLE; }

private:
    void createTextureImage(const std::string& filepath);
    void createTextureImageView();
    void createTextureSampler();
    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void  createIndexBuffer(const std::vector<uint32_t>& indices);
};

}   //  end of the VKModel namespace
