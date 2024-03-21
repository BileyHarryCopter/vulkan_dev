#include <cassert>
#include <cstring>

#include "model.hpp"
#include "buffmanager.hpp"

namespace VKBuffmanager
{
    VkDeviceSize Buffmanager::getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) 
    {
        if (minOffsetAlignment > 0)
            return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
        return instanceSize;
    }
 
    Buffmanager::Buffmanager(VKDevice::Device &device, VkDeviceSize instanceSize, uint32_t instanceCount,
                             VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize minOffsetAlignment)
    : device_{device}, instancesize_{instanceSize}, instancecount_{instanceCount}, usageflags_{usageFlags}, memorypropertyflags_{memoryPropertyFlags} 
    {
        alignmentsize_ = getAlignment(instanceSize, minOffsetAlignment);
        buffersize_ = alignmentsize_ * instanceCount;
        device_.createBuffer(buffersize_, usageFlags, memoryPropertyFlags, buffer_, memory_);
    }
 
    Buffmanager::~Buffmanager() 
    {
        unmap();
        vkDestroyBuffer(device_.get_logic(), buffer_, nullptr);
        vkFreeMemory   (device_.get_logic(), memory_, nullptr);
    }

    VkResult Buffmanager::map(VkDeviceSize size, VkDeviceSize offset) 
    {
        assert(buffer_ && memory_ && "Called map on buffer before create");
        return vkMapMemory(device_.get_logic(), memory_, offset, size, 0, &mapped_);
    }
 
    void Buffmanager::unmap() 
    {
        if (mapped_) 
        {
            vkUnmapMemory(device_.get_logic(), memory_);
            mapped_ = nullptr;
        }
    }
 
    void Buffmanager::writeToBuffer(void *data, VkDeviceSize size, VkDeviceSize offset) 
    {
        assert(mapped_ && "Cannot copy to unmapped buffer");
        
        if (size == VK_WHOLE_SIZE)
            memcpy(mapped_, data, buffersize_);
        else 
        {
            char *memOffset = (char *) mapped_;
            memOffset += offset;
            memcpy(memOffset, data, size);
        }
    }
 
    VkResult Buffmanager::flush(VkDeviceSize size, VkDeviceSize offset) 
    {
        VkMappedMemoryRange mappedRange {};
        mappedRange.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory =                               memory_;
        mappedRange.offset =                                offset;
        mappedRange.size   =                                  size;
        return vkFlushMappedMemoryRanges(device_.get_logic(), 1, &mappedRange);
    }

    VkResult Buffmanager::invalidate(VkDeviceSize size, VkDeviceSize offset) 
    {
        VkMappedMemoryRange mappedRange {};
        mappedRange.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory =                               memory_;
        mappedRange.offset =                                offset;
        mappedRange.size   =                                  size;
        return vkInvalidateMappedMemoryRanges(device_.get_logic(), 1, &mappedRange);
    }
 

    VkDescriptorBufferInfo Buffmanager::descriptorInfo(VkDeviceSize size, VkDeviceSize offset) 
    {
        return VkDescriptorBufferInfo{buffer_, offset, size, };
    }
 

    void Buffmanager::writeToIndex(void *data, int index)  { writeToBuffer(data, instancesize_, index * alignmentsize_); }
    VkResult Buffmanager::flushIndex(int index)            {       return flush(alignmentsize_, index * alignmentsize_); }
    VkDescriptorBufferInfo Buffmanager::descriptorInfoForIndex(int index) { return descriptorInfo(alignmentsize_, index * alignmentsize_); }
    VkResult Buffmanager::invalidateIndex(int index)                      {     return invalidate(alignmentsize_, index * alignmentsize_); }
    
}   //  end of namespace VKBuffmanager
