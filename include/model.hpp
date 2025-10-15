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

    bool hasmeshlets = false;
    std::unique_ptr<VKBuffmanager::Buffmanager> meshletbuff_;
    std::unique_ptr<VKBuffmanager::Buffmanager> meshletverticesbuff_;
    std::unique_ptr<VKBuffmanager::Buffmanager> meshlettrianglesbuff_;
    uint32_t meshletcount_ = 0;

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

        static std::vector<VkVertexInputBindingDescription>     get_binding_descriptions();
        static std::vector<VkVertexInputAttributeDescription> get_attribute_descriptions();

        bool operator== (const Vertex& rhs) const 
        {
            return position == rhs.position && color == rhs.color && normal == rhs.normal && uv == rhs.uv;
        }
    };

    struct Meshlet
    {
        uint32_t vertexOffset;
        uint32_t vertexCount;
        uint32_t primitiveOffset;
        uint32_t primitiveCount;
    };

    struct Builder 
    {
        std::vector<Vertex>   vertices{};
        std::vector<uint32_t>  indices{};
        std::vector<Meshlet>  meshlets{};
        std::vector<uint32_t> meshletVertices{};
        std::vector<uint8_t>  meshletTriangles{};

        std::string  filepath_to_texture;

        void load_models (const std::string& filepath_to_model);
        void generate_meshlets();
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
    void draw_meshlets(VkCommandBuffer commandbuffer);

    VkImageView getimgview() { return textureimgview_; }
    VkSampler   getsampler() { return texturesampler_; }
    bool has_texture() { return textureimg_ != VK_NULL_HANDLE; }
    bool has_meshlets() { return hasmeshlets; }
    uint32_t get_meshlet_count() { return meshletcount_; }
    
    VkBuffer get_meshlet_buffer() { return hasmeshlets ? meshletbuff_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer get_meshlet_vertices_buffer() { return hasmeshlets ? meshletverticesbuff_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer get_meshlet_triangles_buffer() { return hasmeshlets ? meshlettrianglesbuff_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer get_vertex_buffer() { return vertexbuff_->getBuffer(); }
    
    VkDescriptorBufferInfo get_meshlet_buffer_info() { return hasmeshlets ? meshletbuff_->descriptorInfo() : VkDescriptorBufferInfo{}; }
    VkDescriptorBufferInfo get_meshlet_vertices_buffer_info() { return hasmeshlets ? meshletverticesbuff_->descriptorInfo() : VkDescriptorBufferInfo{}; }
    VkDescriptorBufferInfo get_meshlet_triangles_buffer_info() { return hasmeshlets ? meshlettrianglesbuff_->descriptorInfo() : VkDescriptorBufferInfo{}; }
    VkDescriptorBufferInfo get_vertex_buffer_info() { return vertexbuff_->descriptorInfo(); }

private:
    void createTextureImage(const std::string& filepath);
    void createTextureImageView();
    void createTextureSampler();
    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void  createIndexBuffer(const std::vector<uint32_t>& indices);
    void createMeshletBuffers(const std::vector<Meshlet>& meshlets, 
                              const std::vector<uint32_t>& meshletVertices,
                              const std::vector<uint8_t>& meshletTriangles);
};

}   //  end of the VKModel namespace
