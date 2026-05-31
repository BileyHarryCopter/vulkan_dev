#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace VKLod
{

// Checkpoints for incremental LOD integration.
// Each phase is designed to be a stable "stop-and-verify" point.
enum class IntegrationPhase : uint32_t
{
    BaselineCulling = 0,     // Existing object/meshlet frustum culling path
    DataAndCacheApi = 1,     // LOD data model + cache format wiring
    OfflineDiscreteLod = 2,  // Offline baker with discrete LOD levels
    RuntimeDiscreteLod = 3,  // Runtime LOD selection on CPU
    HierarchicalCpuLod = 4,  // Cluster/group hierarchy traversal on CPU
    GpuDrivenLod = 5         // Optional compute traversal + compaction
};

struct RuntimeSettings
{
    IntegrationPhase phase = IntegrationPhase::BaselineCulling;
    bool enabled = false;
    int forcedLevel = -1;    // -1 means auto selection
    float targetPixels = 1.5f;
    bool debugLogs = false;
    float debugIntervalSeconds = 1.0f;
    uint32_t debugMaxObjects = 3;
    // Mixed-LOD sensitivity multiplier:
    // >1.0 switches to coarser levels earlier (more responsive),
    // <1.0 keeps finer levels longer.
    float mixedResponse = 1.0f;
};

inline IntegrationPhase parsePhaseFromEnv()
{
    const char* raw = std::getenv("VK_LOD_PHASE");
    if (!raw) return IntegrationPhase::BaselineCulling;

    const int v = std::atoi(raw);
    if (v <= 0) return IntegrationPhase::BaselineCulling;
    if (v == 1) return IntegrationPhase::DataAndCacheApi;
    if (v == 2) return IntegrationPhase::OfflineDiscreteLod;
    if (v == 3) return IntegrationPhase::RuntimeDiscreteLod;
    if (v == 4) return IntegrationPhase::HierarchicalCpuLod;
    return IntegrationPhase::GpuDrivenLod;
}

inline const char* phaseName(IntegrationPhase phase)
{
    switch (phase)
    {
        case IntegrationPhase::BaselineCulling: return "BaselineCulling";
        case IntegrationPhase::DataAndCacheApi: return "DataAndCacheApi";
        case IntegrationPhase::OfflineDiscreteLod: return "OfflineDiscreteLod";
        case IntegrationPhase::RuntimeDiscreteLod: return "RuntimeDiscreteLod";
        case IntegrationPhase::HierarchicalCpuLod: return "HierarchicalCpuLod";
        case IntegrationPhase::GpuDrivenLod: return "GpuDrivenLod";
        default: return "Unknown";
    }
}

// Binary cache format for offline-generated LOD data (*.vklod).
// This file contract is introduced in DataAndCacheApi phase and
// will be consumed by offline baker + runtime loader in next phases.
inline constexpr uint32_t VKLOD_MAGIC = 0x564B4C44;  // "VKLD"
inline constexpr uint32_t VKLOD_VERSION = 2;

struct FileHeader
{
    uint32_t magic = VKLOD_MAGIC;
    uint32_t version = VKLOD_VERSION;
    uint64_t fileSize = 0;
    uint64_t checksum = 0;

    uint32_t levelCount = 0;
    uint32_t groupCount = 0;
    uint32_t nodeCount = 0;
    uint32_t reserved0 = 0;

    uint64_t levelsOffset = 0;
    uint64_t groupsOffset = 0;
    uint64_t nodesOffset = 0;
    uint64_t groupMeshletIndicesOffset = 0;
    uint64_t blobsOffset = 0;
};

struct LodLevel
{
    uint32_t levelIndex = 0;
    float simplifyRatio = 1.0f;
    float maxError = 0.0f;
    uint32_t groupOffset = 0;
    uint32_t groupCount = 0;
    uint32_t nodeOffset = 0;
    uint32_t nodeCount = 0;
    uint32_t rootNode = 0;
    uint32_t reserved0 = 0;
    // Relative to FileHeader::blobsOffset
    uint64_t blobOffset = 0;
    uint64_t blobSize = 0;
};

struct LodGroup
{
    // Group bounds and error for runtime traversal.
    std::array<float, 3> aabbMin{0.f, 0.f, 0.f};
    std::array<float, 3> aabbMax{0.f, 0.f, 0.f};
    std::array<float, 4> boundingSphere{0.f, 0.f, 0.f, 0.f};  // xyz + radius
    float cumulativeError = 0.0f;

    // Range into meshlet draw packets.
    uint32_t meshletOffset = 0;
    uint32_t meshletCount = 0;
    uint32_t materialIndex = 0;
    uint32_t reserved0 = 0;
};

struct LodNode
{
    // Hierarchical traversal node (for phase 4+).
    std::array<float, 4> boundingSphere{0.f, 0.f, 0.f, 0.f};
    float maxError = 0.0f;
    uint32_t firstChild = 0;
    uint16_t childCount = 0;
    uint16_t level = 0;
    uint32_t groupIndex = 0;  // leaf: valid group index, internal: UINT32_MAX
};

struct LodInfo
{
    bool available = false;
    uint32_t version = 0;
    uint32_t levelCount = 0;
    uint32_t groupCount = 0;
    uint32_t nodeCount = 0;
    std::string sourcePath;
};

struct LodData
{
    FileHeader header{};
    std::vector<LodLevel> levels;
    std::vector<LodGroup> groups;
    std::vector<LodNode> nodes;
};

}  // namespace VKLod
