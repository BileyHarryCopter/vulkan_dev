#include "descriptors.hpp"
#include "swapchain.hpp"

#include <cassert>
#include <stdexcept>

namespace VKDescriptors
{


//  builder of descriptor set layout    //
    DescriptorSetLayout::Builder &DescriptorSetLayout::Builder::addBinding(uint32_t binding, VkDescriptorType descriptorType,
                                                                           VkShaderStageFlags stageFlags, uint32_t count) 
    {
        assert(bindings.count(binding) == 0 && "Binding already in use");

        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding         =        binding;
        layoutBinding.descriptorType  = descriptorType;
        layoutBinding.descriptorCount =          count;
        layoutBinding.stageFlags      =     stageFlags;
        bindings[binding]             =  layoutBinding;

        return *this;
    }

    std::unique_ptr<DescriptorSetLayout> DescriptorSetLayout::Builder::build() const 
    {
        return std::make_unique<DescriptorSetLayout>(device_, bindings);
    }


//  descriptor set layout   //
    DescriptorSetLayout::DescriptorSetLayout(VKDevice::Device & device, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
    : device_{device}, bindings{bindings} 
    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
        for (auto kv : bindings)
            setLayoutBindings.push_back(kv.second);

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
        descriptorSetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutInfo.bindingCount =     static_cast<uint32_t>(setLayoutBindings.size());
        descriptorSetLayoutInfo.pBindings    =                            setLayoutBindings.data();
 
        if (vkCreateDescriptorSetLayout(device_.get_logic(), &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("failed to create descriptor set layout!");
    }
 
    DescriptorSetLayout::~DescriptorSetLayout() 
    {
        vkDestroyDescriptorSetLayout(device_.get_logic(), descriptorSetLayout, nullptr);
    }


//  builder of descriptor pool  //
    DescriptorPool::Builder &DescriptorPool::Builder::addPoolSize(VkDescriptorType descriptorType, uint32_t count) 
    {
        poolSizes.push_back({descriptorType, count});
        return *this;
    }
 
    DescriptorPool::Builder &DescriptorPool::Builder::setPoolFlags(VkDescriptorPoolCreateFlags flags) 
    {
        poolFlags = flags;
        return *this;
    }

    DescriptorPool::Builder &DescriptorPool::Builder::setMaxSets(uint32_t count) 
    {
        maxSets = count;
        return *this;
    }
 
    std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const 
    {
        return std::make_unique<DescriptorPool>(device_, maxSets, poolFlags, poolSizes);
    }


//  descriptor pool   //
    DescriptorPool::DescriptorPool(VKDevice::Device & device, uint32_t maxSets, VkDescriptorPoolCreateFlags poolFlags, 
                                         const std::vector<VkDescriptorPoolSize> &poolSizes) : device_{device} 
    {
        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount =       static_cast<uint32_t>(poolSizes.size());
        descriptorPoolInfo.pPoolSizes    =                              poolSizes.data();
        descriptorPoolInfo.maxSets       =                                       maxSets;
        descriptorPoolInfo.flags         =                                     poolFlags;
 
        if (vkCreateDescriptorPool(device_.get_logic(), &descriptorPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("failed to create descriptor pool!");
    }
 
    DescriptorPool::~DescriptorPool() 
    {
        vkDestroyDescriptorPool(device_.get_logic(), descriptorPool, nullptr);
    }
 
    bool DescriptorPool::allocateDescriptor(const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet &descriptor) const 
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     =                                 descriptorPool;
        allocInfo.pSetLayouts        =                           &descriptorSetLayout;
        allocInfo.descriptorSetCount =                                              1;
        
        if (vkAllocateDescriptorSets(device_.get_logic(), &allocInfo, &descriptor) != VK_SUCCESS)
            return false;

        return true;
    }
 
    void DescriptorPool::freeDescriptors(std::vector<VkDescriptorSet> &descriptors) const 
    {
        vkFreeDescriptorSets(device_.get_logic(), descriptorPool, static_cast<uint32_t>(descriptors.size()), descriptors.data());
    }
    
    void DescriptorPool::resetPool()
    {
        vkResetDescriptorPool(device_.get_logic(), descriptorPool, 0);
    }


//  descriptor writer   //
 
    DescriptorWriter::DescriptorWriter(DescriptorSetLayout &setLayout, DescriptorPool &pool)
    : setLayout{setLayout}, pool{pool} {}
    
    DescriptorWriter &DescriptorWriter::writeBuffer(uint32_t binding, VkDescriptorBufferInfo *bufferInfo) 
    {
        assert(setLayout.bindings.count(binding) == 1 && "Layout does not contain specified binding");
    
        auto &bindingDescription = setLayout.bindings[binding];
    
        assert(bindingDescription.descriptorCount == 1 && "Binding single descriptor info, but binding expects multiple");
    
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType  =      bindingDescription.descriptorType;
        write.dstBinding      =                                binding;
        write.pBufferInfo     =                             bufferInfo;
        write.descriptorCount =                                      1;
    
        writes.push_back(write);

        return *this;
    }
 
    DescriptorWriter &DescriptorWriter::writeImage(uint32_t binding, VkDescriptorImageInfo *imageInfo)
    {
        assert(setLayout.bindings.count(binding) == 1 && "Layout does not contain specified binding");
        
        auto &bindingDescription = setLayout.bindings[binding];
        
        assert(bindingDescription.descriptorCount == 1 && "Binding single descriptor info, but binding expects multiple");
        
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType  =      bindingDescription.descriptorType;
        write.dstBinding      =                                binding;
        write.pImageInfo      =                              imageInfo;
        write.descriptorCount =                                      1;
        
        writes.push_back(write);

        return *this;
    }
 
    bool DescriptorWriter::build(VkDescriptorSet &set) 
    {
        bool success = pool.allocateDescriptor(setLayout.getDescriptorSetLayout(), set);
        if (!success)
            return false;

        overwrite(set);
        return true;
    }
 
    void DescriptorWriter::overwrite(VkDescriptorSet &set) 
    {
        for (auto &write : writes)
            write.dstSet = set;

        vkUpdateDescriptorSets(pool.device_.get_logic(), writes.size(), writes.data(), 0, nullptr);
    }


}   //  end of namespace VKDescriptors
