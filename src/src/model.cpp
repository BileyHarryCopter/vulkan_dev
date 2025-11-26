#include "model.hpp"

#include "utility.hpp"

#define TINYOBJLOADER_IMPOLEMENTATION
#include "tinyobjloader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace std
{

template<>
struct hash<VKModel::Model::Vertex>
{
    size_t operator() (const VKModel::Model::Vertex& vertex) const
    {
        size_t seed = 0;
        Service::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
        return seed;
    }
};

}

namespace VKModel
{

    Model::Model (VKDevice::Device& device, const VKModel::Model::Builder& builder) : device_{device}
    {
        if (!builder.filepath_to_texture.empty())
        {
            createTextureImage    (builder.filepath_to_texture);
            createTextureImageView();
            createTextureSampler  ();
        }
        createVertexBuffer    (builder.vertices);
        createIndexBuffer     (builder.indices);
        
#ifdef USE_MESH_SHADING
        // Build meshlets for mesh shading
        if (!builder.indices.empty() && !builder.vertices.empty())
        {
            buildMeshlets(builder.vertices, builder.indices);
            createMeshletBuffers();
        }
#endif
    }

    Model::~Model()
    {
        vkDestroySampler  (device_.get_logic(), texturesampler_, nullptr);
        vkDestroyImageView(device_.get_logic(), textureimgview_, nullptr);

        vkDestroyImage(device_.get_logic(),    textureimg_, nullptr);
        vkFreeMemory  (device_.get_logic(), textureimgmem_, nullptr);
    }

    std::unique_ptr<Model> Model::createModelfromFile (VKDevice::Device& device,const std::string& filepath_to_model, 
                                                                                const std::string& filepath_to_texture = std::string{})
    {
        Builder builder{};
        builder.filepath_to_texture = filepath_to_texture;
        builder.load_models(filepath_to_model);

        return std::make_unique<Model> (device, builder);
    }

    void Model::createTextureImage(const std::string& filepath)
    {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels        = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imagesize =                                                         texWidth * texHeight * 4;

        if (!pixels)
            throw std::runtime_error("failed to load texture image!");

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        device_.createBuffer(imagesize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     stagingBuffer, stagingBufferMemory);
        
        void* mapping_data;
        vkMapMemory(device_.get_logic(), stagingBufferMemory, 0, imagesize, 0, &mapping_data);
        memcpy(mapping_data, pixels, static_cast<size_t>(imagesize));
        vkUnmapMemory(device_.get_logic(), stagingBufferMemory);

        stbi_image_free(pixels);

        device_.createImage (texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     textureimg_, textureimgmem_);

        device_.transitionImageLayout(textureimg_, VK_FORMAT_R8G8B8A8_SRGB,            VK_IMAGE_LAYOUT_UNDEFINED,     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        device_.copyBufferToImage(stagingBuffer,               textureimg_,      static_cast<uint32_t>(texWidth),         static_cast<uint32_t>(texHeight));
        device_.transitionImageLayout(textureimg_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device_.get_logic(), stagingBuffer, nullptr);
        vkFreeMemory   (device_.get_logic(), stagingBufferMemory, nullptr);
    }

    void Model::createTextureImageView()
    {
        textureimgview_ = device_.createImageView(textureimg_, VK_FORMAT_R8G8B8A8_SRGB);
    }

    void Model::createTextureSampler()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device_.get_phys(), &properties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType                   =  VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter               =                       VK_FILTER_LINEAR;
        samplerInfo.minFilter               =                       VK_FILTER_LINEAR;
        samplerInfo.addressModeU            =         VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV            =         VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW            =         VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable        =                                VK_TRUE;
        samplerInfo.maxAnisotropy           = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor             =       VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates =                               VK_FALSE;
        samplerInfo.compareEnable           =                               VK_FALSE;
        samplerInfo.compareOp               =                   VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode              =          VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(device_.get_logic(), &samplerInfo, nullptr, &texturesampler_) != VK_SUCCESS)
            throw std::runtime_error("failed to create texture sampler!");
    }

    void Model::createVertexBuffer(const std::vector<Vertex>& vertices)
    {
        vertexcount_ = static_cast<uint32_t>(vertices.size());
        assert(vertexcount_ >= 3 && "Vertex count must be at least 3\n");

        uint32_t vertexsize   = sizeof(vertices[0]);
        VkDeviceSize buffsize = vertexsize * vertexcount_;

        //  creation of staging buffer in the GPU host
        VKBuffmanager::Buffmanager stagingBuffer {device_, vertexsize, vertexcount_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

        stagingBuffer.map();
        stagingBuffer.writeToBuffer((void *) vertices.data());

        //  creation of buffer for the vertices on the GPU; device local and optimal memory
#ifdef USE_MESH_SHADING
        // For mesh shading, also support storage buffer usage
        vertexbuff_ = std::make_unique<VKBuffmanager::Buffmanager> (device_, vertexsize, vertexcount_, 
                                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
#else
        vertexbuff_ = std::make_unique<VKBuffmanager::Buffmanager> (device_, vertexsize, vertexcount_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
#endif
        
        device_.copyBuffer(stagingBuffer.getBuffer(), vertexbuff_->getBuffer(), buffsize);
        
#ifdef USE_MESH_SHADING
        // Debug: print first few vertices to verify data
        std::cout << "Created vertex buffer: " << vertexcount_ << " vertices, size per vertex: " << vertexsize << " bytes" << std::endl;
        if (vertices.size() > 0) {
            const auto& v0 = vertices[0];
            std::cout << "  First vertex: pos(" << v0.position.x << ", " << v0.position.y << ", " << v0.position.z 
                      << "), color(" << v0.color.x << ", " << v0.color.y << ", " << v0.color.z
                      << "), normal(" << v0.normal.x << ", " << v0.normal.y << ", " << v0.normal.z
                      << "), uv(" << v0.uv.x << ", " << v0.uv.y << ")" << std::endl;
        }
        if (vertices.size() > 1) {
            const auto& v1 = vertices[1];
            std::cout << "  Second vertex: pos(" << v1.position.x << ", " << v1.position.y << ", " << v1.position.z 
                      << "), uv(" << v1.uv.x << ", " << v1.uv.y << ")" << std::endl;
        }
#endif
    }

    void Model::createIndexBuffer(const std::vector<uint32_t> &indices) 
    {
        indexcount_    = static_cast<uint32_t>(indices.size());
        hasindexbuffer = indexcount_ > 0;

        if (!hasindexbuffer)
            return;

        uint32_t  indexsize   = sizeof(indices[0]);
        VkDeviceSize buffsize = sizeof(indices[0]) * indexcount_;
        
        //  creation of staging buffer in the GPU host
        VKBuffmanager::Buffmanager stagingBuffer {device_, indexsize, indexcount_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

        stagingBuffer.map();
        stagingBuffer.writeToBuffer((void *) indices.data());

        //  creation of buffer for the indices on the GPU; device local and optimal memory
        indexbuff_ = std::make_unique<VKBuffmanager::Buffmanager> (device_, indexsize, indexcount_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        device_.copyBuffer(stagingBuffer.getBuffer(), indexbuff_->getBuffer(), buffsize);
    }

    void Model::draw(VkCommandBuffer commandbuffer)
    {
        if (hasindexbuffer)
            vkCmdDrawIndexed(commandbuffer, indexcount_, 1, 0, 0, 0);
        else
            vkCmdDraw(commandbuffer, vertexcount_, 1, 0, 0);    //  put here some constants
    }

    void Model::bind(VkCommandBuffer commandbuffer)
    {
        VkBuffer buffers[] = {vertexbuff_->getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandbuffer, 0, 1, buffers, offsets);  //  and here too

        if (hasindexbuffer)
            vkCmdBindIndexBuffer(commandbuffer, indexbuff_->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    }

    std::vector<VkVertexInputBindingDescription> Model::Vertex::get_binding_descriptions()
    {
        std::vector<VkVertexInputBindingDescription> bindingDescriptions{};

        bindingDescriptions.push_back({0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX});

        return bindingDescriptions;
    }
    std::vector<VkVertexInputAttributeDescription> Model::Vertex::get_attribute_descriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

        attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)});
        attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, color)});
        attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT,   offsetof(Vertex, normal)});
        attributeDescriptions.push_back({3, 0,    VK_FORMAT_R32G32_SFLOAT,       offsetof(Vertex, uv)});

        return attributeDescriptions;
    }

    void Model::Builder::load_models(const std::string& filepath_to_model)
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        //  parsing of the obj file
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath_to_model.c_str()))
            throw std::runtime_error(warn + err);

        vertices.clear();
        indices.clear();


        std::unordered_map<Vertex, uint32_t> uniqueVertices{};
        for (const auto &shape : shapes)
        {
            for (const auto& index : shape.mesh.indices)
            {
                Vertex vertex{};

                auto vertex_index = index.vertex_index;
                if (vertex_index >= 0)
                {
                    vertex.position = {
                        attrib.vertices[3 * vertex_index + 0],
                        attrib.vertices[3 * vertex_index + 1],
                        attrib.vertices[3 * vertex_index + 2]
                    };

                    vertex.color = {
                        attrib.colors[3 * vertex_index + 0],
                        attrib.colors[3 * vertex_index + 1],
                        attrib.colors[3 * vertex_index + 2]
                    };
                }

                auto normal_index = index.normal_index;
                if (normal_index >= 0)
                {
                    vertex.normal = {
                        attrib.normals[3 * normal_index + 0],
                        attrib.normals[3 * normal_index + 1],
                        attrib.normals[3 * normal_index + 2]
                    };
                }

                auto texcoord_index = index.texcoord_index;
                if (texcoord_index >= 0)
                {
                    vertex.uv = {
                        attrib.texcoords[2 * texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * texcoord_index + 1],
                    };
                }


                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t> (vertices.size());
                    vertices.push_back(vertex);
                }
                indices.push_back(uniqueVertices[vertex]);
            }
        }
    }

#ifdef USE_MESH_SHADING
    void Model::buildMeshlets(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        // Simple meshlet building algorithm without meshoptimizer
        // This gives us full control over the data format for debugging
        
        const size_t max_vertices = 64;
        const size_t max_triangles = 124;
        
        // Reserve space (worst case: one meshlet per triangle)
        size_t triangle_count = indices.size() / 3;
        meshlet_data_.meshlets.reserve(triangle_count);
        meshlet_data_.meshlet_vertices.reserve(indices.size());
        meshlet_data_.meshlet_triangles.reserve(triangle_count);  // Each uint32_t contains one triangle (3 bytes)
        
        // Current meshlet state
        meshopt_Meshlet current_meshlet = {};
        current_meshlet.vertex_offset = 0;
        current_meshlet.triangle_offset = 0;
        current_meshlet.vertex_count = 0;
        current_meshlet.triangle_count = 0;
        
        // Map from global vertex index to local meshlet vertex index
        std::unordered_map<uint32_t, uint8_t> vertex_map;
        
        // Process triangles
        for (size_t i = 0; i < triangle_count; ++i)
        {
            uint32_t v0 = indices[i * 3 + 0];
            uint32_t v1 = indices[i * 3 + 1];
            uint32_t v2 = indices[i * 3 + 2];
            
            // Count how many new vertices this triangle would add
            size_t new_vertices = 0;
            if (vertex_map.find(v0) == vertex_map.end()) new_vertices++;
            if (vertex_map.find(v1) == vertex_map.end()) new_vertices++;
            if (vertex_map.find(v2) == vertex_map.end()) new_vertices++;
            
            // Check if we need to start a new meshlet
            bool need_new_meshlet = (current_meshlet.vertex_count + new_vertices > max_vertices) ||
                                    (current_meshlet.triangle_count >= max_triangles);
            
            if (need_new_meshlet && current_meshlet.triangle_count > 0)
            {
                // Save current meshlet and start a new one
                meshlet_data_.meshlets.push_back(current_meshlet);
                
                // Update offsets for next meshlet
                // Note: triangle_offset is now in uint32_t units, not bytes
                current_meshlet.vertex_offset = meshlet_data_.meshlet_vertices.size();
                current_meshlet.triangle_offset = meshlet_data_.meshlet_triangles.size();
                current_meshlet.vertex_count = 0;
                current_meshlet.triangle_count = 0;
                vertex_map.clear();
            }
            
            // Add vertices to current meshlet (if not already present)
            uint8_t local_v0, local_v1, local_v2;
            
            if (vertex_map.find(v0) == vertex_map.end())
            {
                local_v0 = current_meshlet.vertex_count;
                vertex_map[v0] = local_v0;
                meshlet_data_.meshlet_vertices.push_back(v0);
                current_meshlet.vertex_count++;
            }
            else
            {
                local_v0 = vertex_map[v0];
            }
            
            if (vertex_map.find(v1) == vertex_map.end())
            {
                local_v1 = current_meshlet.vertex_count;
                vertex_map[v1] = local_v1;
                meshlet_data_.meshlet_vertices.push_back(v1);
                current_meshlet.vertex_count++;
            }
            else
            {
                local_v1 = vertex_map[v1];
            }
            
            if (vertex_map.find(v2) == vertex_map.end())
            {
                local_v2 = current_meshlet.vertex_count;
                vertex_map[v2] = local_v2;
                meshlet_data_.meshlet_vertices.push_back(v2);
                current_meshlet.vertex_count++;
            }
            else
            {
                local_v2 = vertex_map[v2];
            }
            
            // Add triangle with local indices, packed into uint32_t
            // Format: uint32_t = (i0) | (i1 << 8) | (i2 << 16)
            uint32_t packed_triangle = static_cast<uint32_t>(local_v0) | 
                                       (static_cast<uint32_t>(local_v1) << 8) | 
                                       (static_cast<uint32_t>(local_v2) << 16);
            meshlet_data_.meshlet_triangles.push_back(packed_triangle);
            current_meshlet.triangle_count++;
        }
        
        // Don't forget the last meshlet
        if (current_meshlet.triangle_count > 0)
        {
            meshlet_data_.meshlets.push_back(current_meshlet);
        }
        
        meshlet_data_.meshlet_count = meshlet_data_.meshlets.size();
        
        std::cout << "Built " << meshlet_data_.meshlet_count << " meshlets from " 
                  << vertices.size() << " vertices and " << indices.size() << " indices" << std::endl;
        std::cout << "  - Total meshlet vertices: " << meshlet_data_.meshlet_vertices.size() << std::endl;
        std::cout << "  - Total meshlet triangles: " << meshlet_data_.meshlet_triangles.size() << std::endl;
        
        // Debug: print first meshlet details
        if (meshlet_data_.meshlet_count > 0)
        {
            const auto& first = meshlet_data_.meshlets[0];
            std::cout << "  - First meshlet: " << first.vertex_count << " vertices, " 
                      << first.triangle_count << " triangles" << std::endl;
            std::cout << "    Vertex offset: " << first.vertex_offset 
                      << ", Triangle offset: " << first.triangle_offset << std::endl;
            
            // Print first triangle indices
            if (first.triangle_count > 0)
            {
                uint32_t tri_offset = first.triangle_offset;
                uint32_t packed = meshlet_data_.meshlet_triangles[tri_offset];
                uint8_t i0 = packed & 0xFF;
                uint8_t i1 = (packed >> 8) & 0xFF;
                uint8_t i2 = (packed >> 16) & 0xFF;
                std::cout << "    First triangle local indices: ["
                          << static_cast<uint32_t>(i0) << ", "
                          << static_cast<uint32_t>(i1) << ", "
                          << static_cast<uint32_t>(i2) << "]" << std::endl;
                std::cout << "    First triangle global vertex indices: ["
                          << meshlet_data_.meshlet_vertices[first.vertex_offset + i0] << ", "
                          << meshlet_data_.meshlet_vertices[first.vertex_offset + i1] << ", "
                          << meshlet_data_.meshlet_vertices[first.vertex_offset + i2] << "]" << std::endl;
                
                // Verify that global vertex indices are within valid range
                uint32_t max_vertex_index = static_cast<uint32_t>(vertices.size() - 1);
                uint32_t v0_idx = meshlet_data_.meshlet_vertices[first.vertex_offset + i0];
                uint32_t v1_idx = meshlet_data_.meshlet_vertices[first.vertex_offset + i1];
                uint32_t v2_idx = meshlet_data_.meshlet_vertices[first.vertex_offset + i2];
                if (v0_idx > max_vertex_index || v1_idx > max_vertex_index || v2_idx > max_vertex_index) {
                    std::cout << "    WARNING: Vertex indices out of range! Max index: " << max_vertex_index << std::endl;
                } else {
                    std::cout << "    Vertex indices are valid (max: " << max_vertex_index << ")" << std::endl;
                }
            }
        }
    }

    void Model::createMeshletBuffers()
    {
        // Create buffer for meshlet structures
        size_t meshlet_size = sizeof(meshopt_Meshlet);
        meshlet_buffer_ = std::make_unique<VKBuffmanager::Buffmanager>(
            device_,
            meshlet_size,
            meshlet_data_.meshlet_count,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        // Create staging buffer and copy meshlet data
        VKBuffmanager::Buffmanager staging_meshlets(
            device_,
            meshlet_size,
            meshlet_data_.meshlet_count,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        staging_meshlets.map();
        staging_meshlets.writeToBuffer((void*)meshlet_data_.meshlets.data());
        device_.copyBuffer(staging_meshlets.getBuffer(), meshlet_buffer_->getBuffer(), 
                          meshlet_size * meshlet_data_.meshlet_count);

        // Create buffer for meshlet vertex indices
        size_t vertex_index_size = sizeof(unsigned int);
        meshlet_vertices_buffer_ = std::make_unique<VKBuffmanager::Buffmanager>(
            device_,
            vertex_index_size,
            meshlet_data_.meshlet_vertices.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VKBuffmanager::Buffmanager staging_vertices(
            device_,
            vertex_index_size,
            meshlet_data_.meshlet_vertices.size(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        staging_vertices.map();
        staging_vertices.writeToBuffer((void*)meshlet_data_.meshlet_vertices.data());
        device_.copyBuffer(staging_vertices.getBuffer(), meshlet_vertices_buffer_->getBuffer(),
                          vertex_index_size * meshlet_data_.meshlet_vertices.size());

        // Create buffer for meshlet triangle indices (packed as uint32_t: each uint32_t contains 3 bytes)
        size_t triangle_size = sizeof(uint32_t);
        meshlet_triangles_buffer_ = std::make_unique<VKBuffmanager::Buffmanager>(
            device_,
            triangle_size,
            meshlet_data_.meshlet_triangles.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VKBuffmanager::Buffmanager staging_triangles(
            device_,
            triangle_size,
            meshlet_data_.meshlet_triangles.size(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        staging_triangles.map();
        staging_triangles.writeToBuffer((void*)meshlet_data_.meshlet_triangles.data());
        device_.copyBuffer(staging_triangles.getBuffer(), meshlet_triangles_buffer_->getBuffer(),
                          triangle_size * meshlet_data_.meshlet_triangles.size());

        // Note: Vertex data is already in vertexbuff_, which supports storage buffer usage
        // The mesh shader will read vertex data using indices from meshlet_vertices_buffer_
        
        std::cout << "Created meshlet buffers: " << meshlet_data_.meshlet_count << " meshlets" << std::endl;
        std::cout << "  - Meshlet structures: " << meshlet_data_.meshlet_count << std::endl;
        std::cout << "  - Meshlet vertices: " << meshlet_data_.meshlet_vertices.size() << std::endl;
        std::cout << "  - Meshlet triangles: " << meshlet_data_.meshlet_triangles.size() << " uint32_t (" 
                  << meshlet_data_.meshlet_triangles.size() << " triangles)" << std::endl;
        
        // Debug: print first few meshlets and triangle indices
        if (meshlet_data_.meshlet_count > 0) {
            const auto& first_meshlet = meshlet_data_.meshlets[0];
            std::cout << "  - First meshlet: " << first_meshlet.vertex_count << " vertices, " 
                      << first_meshlet.triangle_count << " triangles" << std::endl;
            std::cout << "    Vertex offset: " << first_meshlet.vertex_offset 
                      << ", Triangle offset: " << first_meshlet.triangle_offset << std::endl;
            
            // Print first few triangle indices
            if (first_meshlet.triangle_count > 0) {
                std::cout << "    First triangle indices (packed uint32_t): ";
                uint32_t tri_offset = first_meshlet.triangle_offset;
                for (uint32_t i = 0; i < std::min(3u, first_meshlet.triangle_count); ++i) {
                    uint32_t idx = tri_offset + i;
                    if (idx < meshlet_data_.meshlet_triangles.size()) {
                        uint32_t packed = meshlet_data_.meshlet_triangles[idx];
                        uint8_t i0 = packed & 0xFF;
                        uint8_t i1 = (packed >> 8) & 0xFF;
                        uint8_t i2 = (packed >> 16) & 0xFF;
                        std::cout << "[" << i << "]=" 
                                  << static_cast<uint32_t>(i0) << ","
                                  << static_cast<uint32_t>(i1) << ","
                                  << static_cast<uint32_t>(i2) << " ";
                    }
                }
                std::cout << std::endl;
            }
        }
        
        // Debug: print first few meshlets info
        if (meshlet_data_.meshlet_count > 0) {
            std::cout << "  - First meshlet: " << meshlet_data_.meshlets[0].vertex_count 
                      << " vertices, " << meshlet_data_.meshlets[0].triangle_count << " triangles" << std::endl;
            if (meshlet_data_.meshlet_count > 1) {
                std::cout << "  - Second meshlet: " << meshlet_data_.meshlets[1].vertex_count 
                          << " vertices, " << meshlet_data_.meshlets[1].triangle_count << " triangles" << std::endl;
            }
        }
    }
#endif

}   //  end of the VKModel namespace
