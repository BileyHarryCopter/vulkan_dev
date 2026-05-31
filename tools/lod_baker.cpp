#include "lod.hpp"

#include "tinyobjloader.h"

#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{

struct ObjIndexKey
{
    int v = -1;  // geometry-only baking: dedupe by position index first

    bool operator==(const ObjIndexKey& rhs) const
    {
        return v == rhs.v;
    }
};

struct ObjIndexKeyHash
{
    std::size_t operator()(const ObjIndexKey& k) const noexcept
    {
        std::size_t h = static_cast<std::size_t>(k.v + 1);
        return h;
    }
};

struct LevelBuildData
{
    std::vector<std::array<float, 3>> positions;
    std::vector<uint32_t> indices;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint32_t> meshletTrianglesPacked;
    float simplifyRatio = 1.0f;
    float maxError = 0.0f;
    std::array<float, 3> aabbMin{0.f, 0.f, 0.f};
    std::array<float, 3> aabbMax{0.f, 0.f, 0.f};
    std::array<float, 4> sphere{0.f, 0.f, 0.f, 0.f};
};

template <typename T>
void appendRaw(std::vector<uint8_t>& out, const T& value)
{
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), ptr, ptr + sizeof(T));
}

template <typename T>
void appendSpan(std::vector<uint8_t>& out, const std::vector<T>& values)
{
    if (values.empty()) return;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(values.data());
    out.insert(out.end(), ptr, ptr + sizeof(T) * values.size());
}

bool loadObjMesh(const std::string& objPath, std::vector<std::array<float, 3>>& outPositions, std::vector<uint32_t>& outIndices)
{
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(objPath, config))
    {
        if (!reader.Error().empty()) std::cerr << "OBJ parse error: " << reader.Error() << "\n";
        return false;
    }
    if (!reader.Warning().empty())
    {
        const std::string& w = reader.Warning();
        size_t missingMentions = 0;
        size_t pos = 0;
        std::unordered_set<std::string> uniqueNames;
        while ((pos = w.find("material [ '", pos)) != std::string::npos)
        {
            missingMentions++;
            const size_t begin = pos + std::string("material [ '").size();
            const size_t end = w.find("' ] not found in .mtl", begin);
            if (end != std::string::npos) uniqueNames.insert(w.substr(begin, end - begin));
            pos = begin;
        }
        if (missingMentions > 0)
        {
            std::cout << "OBJ parse warning: missing materials in .mtl, mentions="
                      << missingMentions << ", unique=" << uniqueNames.size();
            size_t shown = 0;
            for (const auto& name : uniqueNames)
            {
                if (shown == 0) std::cout << " (examples: ";
                std::cout << name;
                shown++;
                if (shown >= 5 || shown >= uniqueNames.size()) break;
                std::cout << ", ";
            }
            if (shown > 0) std::cout << ")";
            std::cout << "\n";
        }
        else
        {
            std::cout << "OBJ parse warning: " << w << "\n";
        }
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

    outPositions.clear();
    outIndices.clear();

    std::unordered_map<ObjIndexKey, uint32_t, ObjIndexKeyHash> remap;
    remap.reserve(1 << 20);

    for (const auto& shape : shapes)
    {
        for (const auto& idx : shape.mesh.indices)
        {
            ObjIndexKey key{idx.vertex_index};
            auto it = remap.find(key);
            if (it != remap.end())
            {
                outIndices.push_back(it->second);
                continue;
            }

            if (idx.vertex_index < 0) return false;
            const size_t p = static_cast<size_t>(idx.vertex_index) * 3;
            if (p + 2 >= attrib.vertices.size()) return false;

            std::array<float, 3> pos{
                attrib.vertices[p + 0],
                attrib.vertices[p + 1],
                attrib.vertices[p + 2]
            };
            uint32_t newIndex = static_cast<uint32_t>(outPositions.size());
            outPositions.push_back(pos);
            outIndices.push_back(newIndex);
            remap.emplace(key, newIndex);
        }
    }

    if (outPositions.empty() || outIndices.empty()) return false;

    // Secondary weld by exact position value to remove remaining duplicates from source mesh.
    std::vector<uint32_t> vertexRemap(outPositions.size());
    const size_t uniqueVertexCount = meshopt_generateVertexRemap(
        vertexRemap.data(),
        outIndices.data(),
        outIndices.size(),
        outPositions.data(),
        outPositions.size(),
        sizeof(std::array<float, 3>));

    std::vector<std::array<float, 3>> welded(uniqueVertexCount);
    meshopt_remapVertexBuffer(
        welded.data(),
        outPositions.data(),
        outPositions.size(),
        sizeof(std::array<float, 3>),
        vertexRemap.data());
    meshopt_remapIndexBuffer(
        outIndices.data(),
        outIndices.data(),
        outIndices.size(),
        vertexRemap.data());
    outPositions.swap(welded);

    return true;
}

void computeBounds(LevelBuildData& level)
{
    std::array<float, 3> lo{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    std::array<float, 3> hi{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };

    for (const auto& p : level.positions)
    {
        lo[0] = std::min(lo[0], p[0]);
        lo[1] = std::min(lo[1], p[1]);
        lo[2] = std::min(lo[2], p[2]);
        hi[0] = std::max(hi[0], p[0]);
        hi[1] = std::max(hi[1], p[1]);
        hi[2] = std::max(hi[2], p[2]);
    }

    level.aabbMin = lo;
    level.aabbMax = hi;
    const std::array<float, 3> center{
        (lo[0] + hi[0]) * 0.5f,
        (lo[1] + hi[1]) * 0.5f,
        (lo[2] + hi[2]) * 0.5f
    };
    const float dx = hi[0] - center[0];
    const float dy = hi[1] - center[1];
    const float dz = hi[2] - center[2];
    const float radius = std::sqrt(dx * dx + dy * dy + dz * dz);
    level.sphere = {center[0], center[1], center[2], radius};
}

void buildMeshlets(LevelBuildData& level)
{
    const size_t maxVertices = 64;
    const size_t maxTriangles = 124;
    const float coneWeight = 0.5f;

    const size_t maxMeshlets = meshopt_buildMeshletsBound(level.indices.size(), maxVertices, maxTriangles);
    std::vector<meshopt_Meshlet> tempMeshlets(maxMeshlets);
    std::vector<unsigned int> tempMeshletVertices(level.indices.size());
    std::vector<unsigned char> tempMeshletTriangles(maxMeshlets * maxTriangles * 3);

    const size_t meshletCount = meshopt_buildMeshlets(
        tempMeshlets.data(),
        tempMeshletVertices.data(),
        tempMeshletTriangles.data(),
        level.indices.data(),
        level.indices.size(),
        reinterpret_cast<const float*>(level.positions.data()),
        level.positions.size(),
        sizeof(std::array<float, 3>),
        maxVertices,
        maxTriangles,
        coneWeight);

    tempMeshlets.resize(meshletCount);
    if (meshletCount > 0)
    {
        const meshopt_Meshlet& last = tempMeshlets[meshletCount - 1];
        tempMeshletVertices.resize(last.vertex_offset + last.vertex_count);
        tempMeshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);
    }

    level.meshlets = std::move(tempMeshlets);
    level.meshletVertices.assign(tempMeshletVertices.begin(), tempMeshletVertices.end());
    level.meshletTrianglesPacked.clear();
    level.meshletTrianglesPacked.reserve(level.indices.size() / 3);

    for (size_t i = 0; i < level.meshlets.size(); ++i)
    {
        const meshopt_Meshlet m = level.meshlets[i];
        const uint32_t packedOffset = static_cast<uint32_t>(level.meshletTrianglesPacked.size());
        level.meshlets[i].triangle_offset = packedOffset;

        for (size_t t = 0; t < m.triangle_count; ++t)
        {
            const size_t triByteOffset = m.triangle_offset + t * 3;
            const uint32_t i0 = static_cast<uint32_t>(tempMeshletTriangles[triByteOffset + 0]);
            const uint32_t i1 = static_cast<uint32_t>(tempMeshletTriangles[triByteOffset + 1]);
            const uint32_t i2 = static_cast<uint32_t>(tempMeshletTriangles[triByteOffset + 2]);
            level.meshletTrianglesPacked.push_back(i0 | (i1 << 8) | (i2 << 16));
        }
    }
}

LevelBuildData makeLevel(const std::vector<std::array<float, 3>>& basePositions,
                         const std::vector<uint32_t>& baseIndices,
                         float ratio,
                         float baseTargetError)
{
    LevelBuildData level{};
    level.simplifyRatio = ratio;
    level.maxError = 0.0f;

    std::vector<uint32_t> indices = baseIndices;
    if (ratio < 0.999f)
    {
        size_t target = static_cast<size_t>(static_cast<double>(baseIndices.size()) * static_cast<double>(ratio));
        target = std::max<size_t>(3, target - (target % 3));
        target = std::min(target, baseIndices.size());

        std::vector<uint32_t> simplified(indices.size());
        float resultError = 0.0f;
        size_t written = indices.size();
        float targetError = baseTargetError;

        // If mesh doesn't simplify enough, progressively relax target_error.
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            written = meshopt_simplify(
                simplified.data(),
                indices.data(),
                indices.size(),
                reinterpret_cast<const float*>(basePositions.data()),
                basePositions.size(),
                sizeof(std::array<float, 3>),
                target,
                targetError,
                0,
                &resultError);
            written -= (written % 3);

            const bool goodReduction = (written <= static_cast<size_t>(static_cast<double>(indices.size()) * 0.97));
            if (goodReduction || written <= target || targetError > 1.0f) break;
            targetError *= 4.0f;
        }

        simplified.resize(written);
        indices.swap(simplified);
        level.maxError = resultError;
    }

    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), basePositions.size());
    meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(),
                             reinterpret_cast<const float*>(basePositions.data()),
                             basePositions.size(),
                             sizeof(std::array<float, 3>), 1.05f);

    std::vector<uint32_t> remap(basePositions.size());
    const size_t newVertexCount = meshopt_optimizeVertexFetchRemap(
        remap.data(), indices.data(), indices.size(), basePositions.size());

    std::vector<std::array<float, 3>> positions(newVertexCount);
    meshopt_remapVertexBuffer(positions.data(), basePositions.data(), basePositions.size(),
                              sizeof(std::array<float, 3>), remap.data());
    meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());

    level.positions = std::move(positions);
    level.indices = std::move(indices);

    computeBounds(level);
    buildMeshlets(level);
    return level;
}

std::vector<uint8_t> buildLevelBlob(const LevelBuildData& level)
{
    struct LevelBlobHeader
    {
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletCount = 0;
        uint32_t meshletVertexCount = 0;
        uint32_t meshletTriangleCount = 0;
    } header{};

    header.vertexCount = static_cast<uint32_t>(level.positions.size());
    header.indexCount = static_cast<uint32_t>(level.indices.size());
    header.meshletCount = static_cast<uint32_t>(level.meshlets.size());
    header.meshletVertexCount = static_cast<uint32_t>(level.meshletVertices.size());
    header.meshletTriangleCount = static_cast<uint32_t>(level.meshletTrianglesPacked.size());

    std::vector<uint8_t> blob;
    appendRaw(blob, header);
    appendSpan(blob, level.positions);
    appendSpan(blob, level.indices);
    appendSpan(blob, level.meshlets);
    appendSpan(blob, level.meshletVertices);
    appendSpan(blob, level.meshletTrianglesPacked);
    return blob;
}

static uint32_t expandBits10(uint32_t v)
{
    v &= 0x3ffu;
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v << 8)) & 0x0300F00Fu;
    v = (v | (v << 4)) & 0x030C30C3u;
    v = (v | (v << 2)) & 0x09249249u;
    return v;
}

static uint32_t morton3D10(uint32_t x, uint32_t y, uint32_t z)
{
    return (expandBits10(x) << 2) | (expandBits10(y) << 1) | expandBits10(z);
}

static std::array<float, 3> meshletCenter(const LevelBuildData& src, const meshopt_Meshlet& m)
{
    std::array<float, 3> c{0.f, 0.f, 0.f};
    if (m.vertex_count == 0) return c;
    for (uint32_t i = 0; i < m.vertex_count; i++)
    {
        const uint32_t gv = src.meshletVertices[m.vertex_offset + i];
        const auto& p = src.positions[gv];
        c[0] += p[0];
        c[1] += p[1];
        c[2] += p[2];
    }
    const float inv = 1.0f / static_cast<float>(m.vertex_count);
    c[0] *= inv; c[1] *= inv; c[2] *= inv;
    return c;
}

static void computeGroupBounds(const LevelBuildData& src,
                               const std::vector<uint32_t>& meshletIds,
                               size_t begin, size_t end,
                               std::array<float, 3>& outMin,
                               std::array<float, 3>& outMax,
                               std::array<float, 4>& outSphere)
{
    std::array<float, 3> lo{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    std::array<float, 3> hi{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };
    for (size_t k = begin; k < end; k++)
    {
        const meshopt_Meshlet& m = src.meshlets[meshletIds[k]];
        for (uint32_t i = 0; i < m.vertex_count; i++)
        {
            const uint32_t gv = src.meshletVertices[m.vertex_offset + i];
            const auto& p = src.positions[gv];
            lo[0] = std::min(lo[0], p[0]); lo[1] = std::min(lo[1], p[1]); lo[2] = std::min(lo[2], p[2]);
            hi[0] = std::max(hi[0], p[0]); hi[1] = std::max(hi[1], p[1]); hi[2] = std::max(hi[2], p[2]);
        }
    }
    outMin = lo;
    outMax = hi;
    const std::array<float, 3> c{
        (lo[0] + hi[0]) * 0.5f,
        (lo[1] + hi[1]) * 0.5f,
        (lo[2] + hi[2]) * 0.5f
    };
    const float dx = hi[0] - c[0];
    const float dy = hi[1] - c[1];
    const float dz = hi[2] - c[2];
    outSphere = {c[0], c[1], c[2], std::sqrt(dx * dx + dy * dy + dz * dz)};
}

bool writeVklod(const std::string& outPath, const std::vector<LevelBuildData>& levelsBuilt)
{
    std::vector<VKLod::LodLevel> levels;
    std::vector<VKLod::LodGroup> groups;
    std::vector<VKLod::LodNode> nodes;
    std::vector<uint32_t> groupMeshletIndices;
    std::vector<std::vector<uint8_t>> levelBlobs;

    levels.reserve(levelsBuilt.size());
    groups.reserve(levelsBuilt.size() * 64);
    levelBlobs.reserve(levelsBuilt.size());

    constexpr uint32_t GROUP_MESHLET_BUDGET = 256;

    for (size_t i = 0; i < levelsBuilt.size(); ++i)
    {
        const LevelBuildData& src = levelsBuilt[i];
        VKLod::LodLevel level{};
        level.levelIndex = static_cast<uint32_t>(i);
        level.simplifyRatio = src.simplifyRatio;
        level.maxError = src.maxError;
        level.groupOffset = static_cast<uint32_t>(groups.size());
        level.nodeOffset = static_cast<uint32_t>(nodes.size());

        // Spatially coherent grouping of meshlets (morton order), then fixed-size chunks.
        std::vector<uint32_t> meshletOrder(src.meshlets.size());
        std::vector<uint32_t> meshletKey(src.meshlets.size(), 0u);
        for (uint32_t m = 0; m < static_cast<uint32_t>(src.meshlets.size()); m++)
        {
            meshletOrder[m] = m;
            const auto c = meshletCenter(src, src.meshlets[m]);
            const float sx = (src.aabbMax[0] > src.aabbMin[0]) ? (c[0] - src.aabbMin[0]) / (src.aabbMax[0] - src.aabbMin[0]) : 0.5f;
            const float sy = (src.aabbMax[1] > src.aabbMin[1]) ? (c[1] - src.aabbMin[1]) / (src.aabbMax[1] - src.aabbMin[1]) : 0.5f;
            const float sz = (src.aabbMax[2] > src.aabbMin[2]) ? (c[2] - src.aabbMin[2]) / (src.aabbMax[2] - src.aabbMin[2]) : 0.5f;
            const uint32_t qx = static_cast<uint32_t>(std::clamp(sx, 0.0f, 1.0f) * 1023.0f);
            const uint32_t qy = static_cast<uint32_t>(std::clamp(sy, 0.0f, 1.0f) * 1023.0f);
            const uint32_t qz = static_cast<uint32_t>(std::clamp(sz, 0.0f, 1.0f) * 1023.0f);
            meshletKey[m] = morton3D10(qx, qy, qz);
        }
        std::sort(meshletOrder.begin(), meshletOrder.end(), [&](uint32_t a, uint32_t b) {
            return meshletKey[a] < meshletKey[b];
        });

        const size_t groupBegin = groups.size();
        for (size_t begin = 0; begin < meshletOrder.size(); begin += GROUP_MESHLET_BUDGET)
        {
            const size_t end = std::min(begin + static_cast<size_t>(GROUP_MESHLET_BUDGET), meshletOrder.size());
            VKLod::LodGroup group{};
            computeGroupBounds(src, meshletOrder, begin, end, group.aabbMin, group.aabbMax, group.boundingSphere);
            group.cumulativeError = src.maxError;
            group.meshletOffset = static_cast<uint32_t>(groupMeshletIndices.size());
            group.meshletCount = static_cast<uint32_t>(end - begin);
            group.materialIndex = 0;
            for (size_t k = begin; k < end; k++) groupMeshletIndices.push_back(meshletOrder[k]);
            groups.push_back(group);
        }
        level.groupCount = static_cast<uint32_t>(groups.size() - groupBegin);

        // Node hierarchy for this level: one root + leaf nodes for all groups.
        VKLod::LodNode root{};
        root.boundingSphere = src.sphere;
        root.maxError = src.maxError;
        root.firstChild = static_cast<uint32_t>(nodes.size() + 1);
        root.childCount = static_cast<uint16_t>(std::min<size_t>(level.groupCount, 65535));
        root.level = static_cast<uint16_t>(i);
        root.groupIndex = UINT32_MAX;
        const uint32_t rootIndex = static_cast<uint32_t>(nodes.size());
        nodes.push_back(root);
        for (uint32_t g = 0; g < level.groupCount; g++)
        {
            const uint32_t groupIndex = level.groupOffset + g;
            const VKLod::LodGroup& group = groups[groupIndex];
            VKLod::LodNode leaf{};
            leaf.boundingSphere = group.boundingSphere;
            leaf.maxError = group.cumulativeError;
            leaf.firstChild = 0;
            leaf.childCount = 0;
            leaf.level = static_cast<uint16_t>(i);
            leaf.groupIndex = groupIndex;
            nodes.push_back(leaf);
        }
        level.nodeCount = static_cast<uint32_t>(nodes.size()) - level.nodeOffset;
        level.rootNode = rootIndex;

        levels.push_back(level);

        levelBlobs.push_back(buildLevelBlob(src));
    }

    VKLod::FileHeader header{};
    header.levelCount = static_cast<uint32_t>(levels.size());
    header.groupCount = static_cast<uint32_t>(groups.size());
    header.nodeCount = static_cast<uint32_t>(nodes.size());

    header.levelsOffset = sizeof(VKLod::FileHeader);
    header.groupsOffset = header.levelsOffset + sizeof(VKLod::LodLevel) * levels.size();
    header.nodesOffset = header.groupsOffset + sizeof(VKLod::LodGroup) * groups.size();
    header.groupMeshletIndicesOffset = header.nodesOffset + sizeof(VKLod::LodNode) * nodes.size();
    header.blobsOffset = header.groupMeshletIndicesOffset + sizeof(uint32_t) * groupMeshletIndices.size();

    uint64_t blobCursor = 0;
    for (size_t i = 0; i < levels.size(); ++i)
    {
        levels[i].blobOffset = blobCursor;
        levels[i].blobSize = levelBlobs[i].size();
        blobCursor += levels[i].blobSize;
    }
    header.fileSize = header.blobsOffset + blobCursor;

    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open())
    {
        std::cerr << "Failed to open output file: " << outPath << "\n";
        return false;
    }

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!levels.empty()) out.write(reinterpret_cast<const char*>(levels.data()), sizeof(VKLod::LodLevel) * levels.size());
    if (!groups.empty()) out.write(reinterpret_cast<const char*>(groups.data()), sizeof(VKLod::LodGroup) * groups.size());
    if (!nodes.empty()) out.write(reinterpret_cast<const char*>(nodes.data()), sizeof(VKLod::LodNode) * nodes.size());
    if (!groupMeshletIndices.empty()) {
        out.write(reinterpret_cast<const char*>(groupMeshletIndices.data()),
                  static_cast<std::streamsize>(sizeof(uint32_t) * groupMeshletIndices.size()));
    }
    for (const auto& blob : levelBlobs)
    {
        if (!blob.empty()) out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    }

    const bool ok = out.good();
    out.close();
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: lod_baker <input.obj> <output.vklod>\n";
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = argv[2];

    std::vector<std::array<float, 3>> basePositions;
    std::vector<uint32_t> baseIndices;
    if (!loadObjMesh(inputPath, basePositions, baseIndices))
    {
        std::cerr << "Failed to load OBJ mesh: " << inputPath << "\n";
        return 2;
    }

    std::cout << "Loaded OBJ: vertices=" << basePositions.size()
              << " indices=" << baseIndices.size() << "\n";

    const std::array<float, 4> ratios{1.0f, 0.5f, 0.25f, 0.125f};
    const std::array<float, 4> targetErrors{0.0f, 5e-3f, 2e-2f, 8e-2f};
    std::vector<LevelBuildData> levelsBuilt;
    levelsBuilt.reserve(ratios.size());

    for (size_t i = 0; i < ratios.size(); ++i)
    {
        LevelBuildData level = makeLevel(basePositions, baseIndices, ratios[i], targetErrors[i]);
        std::cout << "LOD" << i << " ratio=" << ratios[i]
                  << " vertices=" << level.positions.size()
                  << " indices=" << level.indices.size()
                  << " meshlets=" << level.meshlets.size()
                  << " error=" << level.maxError << "\n";
        levelsBuilt.push_back(std::move(level));
    }

    if (!writeVklod(outputPath, levelsBuilt))
    {
        std::cerr << "Failed to write vklod: " << outputPath << "\n";
        return 3;
    }

    std::cout << "Wrote vklod: " << outputPath << "\n";
    return 0;
}
