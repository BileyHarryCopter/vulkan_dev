#pragma once

#include <memory>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "lod.hpp"

#ifdef USE_MESH_SHADING
#include "meshoptimizer.h"
#endif

#include "device.hpp"
#include "buffmanager.hpp"


namespace VKModel
{

// Material structure for MTL file data
struct Material {
    glm::vec3 diffuseColor;      // Kd - diffuse color
    glm::vec3 specularColor;      // Ks - specular color
    float shininess;              // Ns - shininess (specular exponent)
    float dissolve;               // d - dissolve (transparency, 1.0 = opaque, 0.0 = transparent)
    
    Material() : diffuseColor(1.0f, 1.0f, 1.0f), specularColor(0.5f, 0.5f, 0.5f), shininess(5.0f), dissolve(1.0f) {}
};

// Submesh structure for parts of model with different materials
struct Submesh {
    uint32_t indexOffset;         // Offset in index buffer
    uint32_t indexCount;          // Number of indices
    uint32_t materialIndex;       // Index into materials array
    
    Submesh() : indexOffset(0), indexCount(0), materialIndex(0) {}
};

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

    // Materials and submeshes
    std::vector<Material> materials_;
    std::vector<Submesh> submeshes_;

    // Axis-aligned bounding box in model space (for frustum culling)
    glm::vec3 aabbMin_{0.f};
    glm::vec3 aabbMax_{0.f};
    VKLod::LodInfo lodInfo_{};
    std::vector<VKLod::LodLevel> lodLevels_;
    std::vector<VKLod::LodGroup> lodGroups_;
    std::vector<VKLod::LodNode> lodNodes_;
    std::vector<uint32_t> lodGroupMeshletIndices_;
    uint32_t lodRootNode_ = 0;

#ifdef USE_MESH_SHADING
    // Meshlet data for mesh shading
    struct MeshletData
    {
        std::vector<meshopt_Meshlet> meshlets;
        std::vector<unsigned int> meshlet_vertices;  // Vertex indices per meshlet
        std::vector<uint32_t> meshlet_triangles;  // Packed triangle indices: each uint32_t contains 3 bytes (i0, i1, i2) + 1 byte padding
        std::vector<glm::vec3> meshlet_aabb_min;  // AABB min per meshlet (model space) for culling
        std::vector<glm::vec3> meshlet_aabb_max;
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
        std::vector<Material> materials{};
        std::vector<Submesh> submeshes{};

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
    // Future path: load pre-baked LOD cache (*.vklod).
    static std::unique_ptr<Model> createModelFromLodCache(VKDevice::Device& device, const std::string& lodCachePath,
                                                           const std::string& filepath_to_texture = std::string{});

    void bind(VkCommandBuffer commandbuffer) const;
    void draw(VkCommandBuffer commandbuffer) const;
    void drawSubmesh(VkCommandBuffer commandbuffer, uint32_t submeshIndex) const;

    VkImageView getimgview() { return textureimgview_; }
    VkSampler   getsampler() { return texturesampler_; }
    bool has_texture() { return textureimg_ != VK_NULL_HANDLE; }
    
    const std::vector<Material>& getMaterials() const { return materials_; }
    const std::vector<Submesh>& getSubmeshes() const { return submeshes_; }
    uint32_t getSubmeshCount() const { return static_cast<uint32_t>(submeshes_.size()); }

    /** Bounding box in model space (min and max corners). Used for frustum culling. */
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const { outMin = aabbMin_; outMax = aabbMax_; }
    bool hasLod() const { return lodInfo_.available; }
    const VKLod::LodInfo& getLodInfo() const { return lodInfo_; }
    bool hasLodHierarchy() const { return lodInfo_.available && !lodNodes_.empty() && !lodGroups_.empty(); }
    const std::vector<VKLod::LodLevel>& getLodLevels() const { return lodLevels_; }
    const std::vector<VKLod::LodGroup>& getLodGroups() const { return lodGroups_; }
    const std::vector<VKLod::LodNode>& getLodNodes() const { return lodNodes_; }
    const std::vector<uint32_t>& getLodGroupMeshletIndices() const { return lodGroupMeshletIndices_; }
    uint32_t getLodRootNode() const { return lodRootNode_; }

#ifdef USE_MESH_SHADING
    VkBuffer getMeshletBuffer() const { return meshlet_buffer_ ? meshlet_buffer_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer getMeshletVerticesBuffer() const { return meshlet_vertices_buffer_ ? meshlet_vertices_buffer_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer getMeshletTrianglesBuffer() const { return meshlet_triangles_buffer_ ? meshlet_triangles_buffer_->getBuffer() : VK_NULL_HANDLE; }
    VkBuffer getVertexDataBuffer() const { return vertexbuff_ ? vertexbuff_->getBuffer() : VK_NULL_HANDLE; }
    uint32_t getMeshletCount() const { return meshlet_data_.meshlet_count; }
    /** AABB of meshlet i in model space (for meshlet frustum culling). */
    void getMeshletAABB(uint32_t i, glm::vec3& outMin, glm::vec3& outMax) const;
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
