#pragma once

#include "defines.h"

using namespace glm;

struct Vertex {
    vec3 pos;
    vec3 color;
    vec2 uv;
    vec3 norm;

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