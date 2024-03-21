#pragma once

#include "device.hpp"

namespace VKBuffmanager
{

class Buffmanager
{   
    VKDevice::Device&                  device_;
    void*             mapped_ =        nullptr;
    VkBuffer          buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory    memory_ = VK_NULL_HANDLE;

    VkDeviceSize                   buffersize_;
    uint32_t                    instancecount_;
    VkDeviceSize                 instancesize_;
    VkDeviceSize                alignmentsize_;
    VkBufferUsageFlags             usageflags_;
    VkMemoryPropertyFlags memorypropertyflags_;

    static VkDeviceSize getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);

public:
    Buffmanager(VKDevice::Device& device, VkDeviceSize instanceSize, uint32_t instanceCount, VkBufferUsageFlags usageFlags,
                  VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize minOffsetAlignment = 1);
    ~Buffmanager();
 
    Buffmanager(const Buffmanager&) = delete;
    Buffmanager& operator=(const Buffmanager&) = delete;
 
    VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    void     unmap();

    void writeToBuffer (void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    VkResult flush (VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    VkDescriptorBufferInfo descriptorInfo (VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    VkResult invalidate (VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

    void writeToIndex (void* data, int index);
    VkResult flushIndex (int index);
    VkDescriptorBufferInfo descriptorInfoForIndex (int index);
    VkResult invalidateIndex (int index);

    VkBuffer                           getBuffer() const              { return buffer_; }
    void*                        getMappedMemory() const              { return mapped_; }
    uint32_t                    getInstanceCount() const       { return instancecount_; }
    VkDeviceSize                 getInstanceSize() const        { return instancesize_; }
    VkDeviceSize                getAlignmentSize() const        { return instancesize_; }
    VkBufferUsageFlags             getUsageFlags() const          { return usageflags_; }
    VkMemoryPropertyFlags getMemoryPropertyFlags() const { return memorypropertyflags_; }
    VkDeviceSize                   getBufferSize() const          { return buffersize_; }
};

}   //  end of namespace VKBuffermanager