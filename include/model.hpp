#pragma once

#include <memory>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#ifdef USE_MESH_SHADING
#include "meshoptimizer.h"
#endif

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

#ifdef USE_MESH_SHADING
    // Meshlet data for mesh shading
    struct MeshletData
    {
        std::vector<meshopt_Meshlet> meshlets;
        std::vector<unsigned int> meshlet_vertices;  // Vertex indices per meshlet
        std::vector<uint32_t> meshlet_triangles;  // Packed triangle indices: each uint32_t contains 3 bytes (i0, i1, i2) + 1 byte padding
        uint32_t meshlet_count = 0;
    };
    
    MeshletData meshlet_data_;
    
    // Storage buffers for meshlet data
    std::unique_ptr<VKBuffmanager::Buffmanager> meshlet_buffer_;  // meshopt_Meshlet array
    std::unique_ptr<VKBuffmanager::Buffmanager> meshlet_vertices_buffer_;  // vertex indices
    std::unique_ptr<VKBuffmanager::Buffmanager> meshlet_triangles_buffer_;  // triangle indices
    std::unique_ptr<VKBuffmanager::Buffmanager> meshlet_vertex_data_buffer_;  // actual vertex data (position, normal, uv, color)
#endif

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

    struct Builder 
    {
        std::vector<Vertex>   vertices{};
        std::vector<uint32_t>  indices{};

        std::string  filepath_to_texture;

        void load_models (const std::string& filepath_to_model);
        
#ifdef USE_MESH_SHADING
        // Store vertex data for meshlet creation
        // This is needed because we need vertex positions for meshopt_buildMeshlets
        std::vector<Vertex> getVertices() const { return vertices; }
#endif
    };

    Model (VKDevice::Device& device, const VKModel::Model::Builder& builder);
    ~Model();

    Model(const Model &rhs) = delete;
    Model &operator=(const Model& rhs) = delete;

    //  function for building a model from obj file and texture
    static std::unique_ptr<Model> createModelfromFile (VKDevice::Device& device,const std::string& filepath_to_model, 
                                                                                const std::string& filepath_to_texture);

    void bind(VkCommandBuffer commandbuffer) const;
    void draw(VkCommandBuffer commandbuffer) const;

    VkImageView getimgview() { return textureimgview_; }
    VkSampler   getsampler() { return texturesampler_; }
    bool has_texture() { return textureimg_ != VK_NULL_HANDLE; }
    
#ifdef USE_MESH_SHADING
    VkBuffer getMeshletBuffer() const { return meshlet_buffer_ ? meshlet_buffer_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer getMeshletVerticesBuffer() const { return meshlet_vertices_buffer_ ? meshlet_vertices_buffer_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer getMeshletTrianglesBuffer() const { return meshlet_triangles_buffer_ ? meshlet_triangles_buffer_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer getVertexDataBuffer() const { return vertexbuff_ ? vertexbuff_->getBuffer() : VK_NULL_HANDLE; }
    uint32_t getMeshletCount() const { return meshlet_data_.meshlet_count; }
#endif

private:
    void createTextureImage(const std::string& filepath);
    void createTextureImageView();
    void createTextureSampler();
    void createDummyTexture();
    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void  createIndexBuffer(const std::vector<uint32_t>& indices);
    
#ifdef USE_MESH_SHADING
    void buildMeshlets(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void createMeshletBuffers();
#endif
};

}   //  end of the VKModel namespace
