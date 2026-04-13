#include "mesh.h"

void Mesh::CreateFromFile(const VulkanReferences& ref, const std::string& path, bool isStorageBuffer) {
    vertices.clear();
    indices.clear();
    this->isStorageBuffer = isStorageBuffer;

    LoadModel(path);
    CreateBuffers(ref);
}
void Mesh::CreateFromArrays(const VulkanReferences& ref, const vector<vec3>& positions, const vector<vec3>& colors, const vector<vec3>& normals, const vector<uint32_t>& indices, bool isStorageBuffer) {
    this->vertices.clear();
    this->indices = indices;
    this->indexCount = this->indices.size();
    this->isStorageBuffer = isStorageBuffer;

    for (int i = 0; i < positions.size(); ++i) {
        this->vertices.push_back(
            Vertex{
                .pos = positions[i],
                .color = colors[i],
                .uv = vec2(0),
                .norm = normals[i]
            }
        );
    }

    CreateBuffers(ref);
}

void Mesh::CreateBuffers(const VulkanReferences& ref) {
    vk::DeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();

    WBuffer vertexStagingBuffer;
    vertexStagingBuffer.Create(ref, vertexBufferSize,
        vk::BufferUsageFlagBits::eTransferSrc, // Can be the SOURCE of a transfer
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vertexStagingBuffer.MapMemory();
    memcpy(vertexStagingBuffer.mappedMemory, vertices.data(), vertexBufferSize);
    vertexStagingBuffer.UnmapMemory();

    vertexBuffer.Create(ref, vertexBufferSize,
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst |
        (isStorageBuffer ? vk::BufferUsageFlagBits::eStorageBuffer : static_cast<vk::BufferUsageFlagBits>(0)),
        vk::MemoryPropertyFlagBits::eDeviceLocal // Device local, can't map memory directly
    );
    vertexBuffer.CopyFrom(ref, vertexStagingBuffer);


    vk::DeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
    // std::cout << "\t" << indices.size() << std::endl;

    WBuffer indexStagingBuffer;
    indexStagingBuffer.Create(ref, indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    indexStagingBuffer.MapMemory();
    memcpy(indexStagingBuffer.mappedMemory, indices.data(), indexBufferSize);
    indexStagingBuffer.UnmapMemory();

    indexBuffer.Create(ref, indexBufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer |
        (isStorageBuffer ? vk::BufferUsageFlagBits::eStorageBuffer : static_cast<vk::BufferUsageFlagBits>(0)),
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    indexBuffer.CopyFrom(ref, indexStagingBuffer);
}

void Mesh::LoadModel(const std::string& path) {
    // Shouldn't be loading when model already loaded
    assert(vertices.size() == 0 && indices.size() == 0);

    tinyobj::attrib_t attrib; // all vert attribs
    vector<tinyobj::shape_t> shapes; // all separate objects and their faces
    vector<tinyobj::material_t> materials; // material/texture per face (that we'll ignore)
    string err;
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str())) {
        throw std::runtime_error(err);
    }
    // LoadObj will auto triangulate n-gons into triangles by default

    // uniqueVerts[v] = Index of v in vertex buffer if exists
    std::unordered_map<Vertex, uint32_t> uniqueVerts;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex = {
                .pos = vec3(
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                ),
                .color = vec3(1.0f),
                .uv = vec2(
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                ),
                .norm = vec3(
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                ),
            };

            // Create vertex if doesn't exist
            if (uniqueVerts.count(vertex) == 0) {
                vertices.push_back(vertex);
                uniqueVerts[vertex] = vertices.size() - 1;
            }

            indices.push_back(uniqueVerts[vertex]);
        }
    }

    indexCount = indices.size();
}