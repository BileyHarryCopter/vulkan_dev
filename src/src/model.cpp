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
        Service::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv, vertex.texIndex);
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
        vertexbuff_ = std::make_unique<VKBuffmanager::Buffmanager> (device_, vertexsize, vertexcount_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        device_.copyBuffer(stagingBuffer.getBuffer(), vertexbuff_->getBuffer(), buffsize);
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
        {
            std::cout << " -- Here we are! -- ";
            std::cout << indexcount_ << std::endl;
            vkCmdDrawIndexed(commandbuffer, indexcount_, 1, 0, 0, 0);
        }
        else
        {
            vkCmdDraw(commandbuffer, vertexcount_, 1, 0, 0);    //  put here some constants
        }
    }

    void Model::bind(VkCommandBuffer commandbuffer)
    {
        VkBuffer buffers[] = {vertexbuff_->getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandbuffer, 0, 1, buffers, offsets);  //  and here too

        if (hasindexbuffer)
        {
            vkCmdBindIndexBuffer(commandbuffer, indexbuff_->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }
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
        attributeDescriptions.push_back({4, 0,         VK_FORMAT_R32_SINT, offsetof(Vertex, texIndex)});

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
            int currentTexIndex = 0;
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

                vertex.texIndex = currentTexIndex;

                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t> (vertices.size());
                    vertices.push_back(vertex);
                }
                indices.push_back(uniqueVertices[vertex]);
            }
            currentTexIndex++;
        }
    }

}   //  end of the VKModel namespace
