#pragma once

#include "defines.h"
#include "scene/buffer.h"

using namespace glm;
using namespace std;

struct Vertex {
    alignas(16) vec3 pos;
    alignas(16) vec3 color;
    alignas(8) vec2 uv;
    alignas(16) vec3 norm;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex }; // binding index, stride, load data per vertex
    }

    static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions() {
        // Location, Binding Index
        return {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)),
            vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, norm))
        };
    }

    bool operator==(const Vertex& other) const {
        return pos == other.pos && color == other.color && uv == other.uv && norm == other.norm;
    }
};

// Hash Function For Vertex
namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.uv) << 1) ^
                (hash<glm::vec3>()(vertex.norm) << 2);
        }
    };
}

// Staging allows us to use high performance memory for loading vertex data
// In practice, not good to do a separate allocation for every object, better to do one big one and split it up (VulkanMemoryAllocator library)
// You should even go a step further, allocate a single vertex and index buffer for lots of things and use offsets to bindvertexbuffers to store lots of 3D objects
class Mesh {
public:
    void CreateFromFile(const VulkanReferences&, const std::string& path, bool isStorageBuffer = false);
    void CreateFromArrays(const VulkanReferences& ref, const vector<vec3>& positions, const vector<vec3>& colors, const vector<vec3>& normals, const vector<uint32_t>& indices, bool isStorageBuffer = false);

    WBuffer vertexBuffer;
    WBuffer indexBuffer;
    uint32_t indexCount;

private:
    void CreateBuffers(const VulkanReferences&);
    void LoadModel(const std::string& path);

    vector<Vertex> vertices;
    vector<uint32_t> indices;

    bool isStorageBuffer;
};