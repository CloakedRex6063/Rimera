#include "model.hpp"
#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"
#include "mikktspace.h"
#include "meshoptimizer.h"
#include "bit"
#include "algorithm"
#include "numeric"
#include "stdexcept"
#include "unordered_map"

namespace
{
    std::span<const std::byte> Bytes(const fastgltf::DataSource& source)
    {
        return std::visit([](const auto& data) -> std::span<const std::byte>
                          {
                              if constexpr (requires { data.bytes; }) return {data.bytes.data(),
                                  data.bytes.size()};
                              else throw std::runtime_error("Unsupported glTF buffer source");
                          },
                          source);
    }

    void CheckData(const fastgltf::Asset& asset)
    {
        for (const auto& view : asset.bufferViews)
        {
            auto bytes = Bytes(asset.buffers.at(view.bufferIndex).data);
            if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset)
                throw std::runtime_error("glTF buffer view exceeds buffer");
        }
        auto check = [&](size_t view_index,
                         size_t offset,
                         size_t count,
                         size_t element_size,
                         size_t stride)
        {
            const auto& view = asset.bufferViews.at(view_index);
            if (!stride || stride < element_size || offset > view.byteLength ||
                (count && (element_size > view.byteLength - offset ||
                           count - 1 > (view.byteLength - offset - element_size) / stride)))
                throw std::runtime_error("glTF accessor exceeds buffer view");
        };
        for (const auto& accessor : asset.accessors)
        {
            auto element_size = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
            if (!element_size) throw std::runtime_error("Invalid glTF accessor element size");
            if (accessor.bufferViewIndex)
            {
                const auto& view = asset.bufferViews.at(*accessor.bufferViewIndex);
                check(*accessor.bufferViewIndex,
                      accessor.byteOffset,
                      accessor.count,
                      element_size,
                      view.byteStride.value_or(element_size));
            }
            if (accessor.sparse)
            {
                const auto& sparse = *accessor.sparse;
                if (sparse.count > accessor.count ||
                    (sparse.indexComponentType != fastgltf::ComponentType::UnsignedByte &&
                     sparse.indexComponentType != fastgltf::ComponentType::UnsignedShort &&
                     sparse.indexComponentType != fastgltf::ComponentType::UnsignedInt))
                    throw std::runtime_error("Invalid sparse accessor indices");
                auto index_size = fastgltf::getComponentByteSize(sparse.indexComponentType);
                check(sparse.indicesBufferView,
                      sparse.indicesByteOffset,
                      sparse.count,
                      index_size,
                      index_size);
                check(sparse.valuesBufferView,
                      sparse.valuesByteOffset,
                      sparse.count,
                      element_size,
                      element_size);
                const auto& index_view = asset.bufferViews.at(sparse.indicesBufferView);
                auto bytes = Bytes(asset.buffers.at(index_view.bufferIndex).data);
                uint32_t previous = 0;
                for (size_t i = 0; i < sparse.count; ++i)
                {
                    uint32_t index = 0;
                    std::memcpy(&index,
                                bytes.data() + index_view.byteOffset + sparse.indicesByteOffset + i
                                * index_size,
                                index_size);
                    if (index >= accessor.count || (i && index <= previous))
                        throw std::runtime_error("Sparse indices must be ordered and in range");
                    previous = index;
                }
            }
        }
    }

    template <class T, class F>
    bool ReadAttribute(const fastgltf::Asset& asset,
                       const fastgltf::Primitive& primitive,
                       std::string_view name,
                       size_t count,
                       fastgltf::AccessorType type,
                       F write)
    {
        auto it = primitive.findAttribute(name);
        if (it == primitive.attributes.end()) return false;
        const auto& accessor = asset.accessors.at(it->accessorIndex);
        if (accessor.count != count || accessor.type != type) throw std::runtime_error(
            "Invalid glTF attribute: " + std::string(name));
        fastgltf::iterateAccessorWithIndex<T>(asset,
                                              accessor,
                                              [&](T value, size_t i) { write(value, i); });
        return true;
    }

    // For normal/tangent generation
    struct SourceVertex
    {
        float position[3]{};
        float normal[3]{};
        float tangent[4]{1, 0, 0, 1};
        float uv[2][2]{};
    };

    void ValidateVertex(const SourceVertex& vertex)
    {
        auto finite = [](const auto& values)
        {
            for (float value : values)
                if (!std::isfinite(value)) throw std::runtime_error("Non-finite vertex attribute");
        };
        finite(vertex.position);
        finite(vertex.normal);
        finite(vertex.tangent);
        for (const auto& uv : vertex.uv)
        {
            finite(uv);
            for (float value : uv)
                if (std::abs(value) > 65504.f) throw std::runtime_error(
                    "UV exceeds finite half-float range");
        }
    }

    float PackDirection(const float* direction, glm::vec3 fallback)
    {
        glm::vec3 unit(direction[0], direction[1], direction[2]);
        float scale = std::max({std::abs(unit.x), std::abs(unit.y), std::abs(unit.z)});
        unit = scale > 0 ? glm::normalize(unit / scale) : fallback;
        uint32_t bits = 0;
        for (unsigned i = 0; i < 3; ++i)
            bits |= (static_cast<uint32_t>(meshopt_quantizeSnorm(unit[i], 10)) & 0x3ffu) << (
                i * 10);
        return std::bit_cast<float>(bits);
    }

    Vertex PackVertex(const SourceVertex& source)
    {
        ValidateVertex(source);
        Vertex result{};
        for (unsigned i = 0; i < 3; ++i) result.position[i] = source.position[i];
        result.normal = PackDirection(source.normal, {0, 0, 1});
        result.tangent = PackDirection(source.tangent, {1, 0, 0});
        result.bitangent = source.tangent[3] < 0 ? 1u : 0u;
        for (unsigned i = 0; i < 2; ++i)
        {
            uint32_t bits = uint32_t(meshopt_quantizeHalf(source.uv[i][0])) |
                            (uint32_t(meshopt_quantizeHalf(source.uv[i][1])) << 16);
            result.uv[i] = bits;
        }
        return result;
    }

    struct TangentData
    {
        std::vector<SourceVertex>* vertices;
        unsigned uv;
    };

    SourceVertex& Corner(const SMikkTSpaceContext* context, int face, int vertex)
    {
        return (*static_cast<TangentData*>(context->m_pUserData)->vertices)[
            static_cast<size_t>(face) * 3 + vertex];
    }

    void GenerateTangents(std::vector<SourceVertex>& vertices, const unsigned uv)
    {
        TangentData data{&vertices, uv};
        SMikkTSpaceInterface interface{};
        interface.m_getNumFaces = [](const SMikkTSpaceContext* c)
        {
            return int(static_cast<TangentData*>(c->m_pUserData)->vertices->size() / 3);
        };
        interface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
        interface.m_getPosition = [](const SMikkTSpaceContext* c, float* out, int f, int v)
        {
            std::copy_n(Corner(c, f, v).position, 3, out);
        };
        interface.m_getNormal = [](const SMikkTSpaceContext* c, float* out, int f, int v)
        {
            std::copy_n(Corner(c, f, v).normal, 3, out);
        };
        interface.m_getTexCoord = [](const SMikkTSpaceContext* c, float* out, int f, int v)
        {
            std::copy_n(Corner(c, f, v).uv[static_cast<TangentData*>(c->m_pUserData)->uv],
                        2,
                        out);
        };
        interface.m_setTSpaceBasic = [](const SMikkTSpaceContext* c,
                                        const float* tangent,
                                        float sign,
                                        int f,
                                        int v)
        {
            auto& vertex = Corner(c, f, v);
            std::copy_n(tangent, 3, vertex.tangent);
            vertex.tangent[3] = sign;
        };
        SMikkTSpaceContext context{&interface, &data};
        if (!genTangSpaceDefault(&context)) throw std::runtime_error("Tangent generation failed");
    }

    Mesh LoadMesh(RM::Device* device,
                  const fastgltf::Asset& asset,
                  const fastgltf::Primitive& primitive,
                  uint32_t material_index,
                  const Material& material)
    {
        if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty() ||
            primitive.dracoCompression)
            throw std::runtime_error("Only uncompressed static triangle primitives are supported");
        auto position = primitive.findAttribute("POSITION");
        if (position == primitive.attributes.end()) throw std::runtime_error(
            "Mesh has no POSITION attribute");
        size_t count = asset.accessors.at(position->accessorIndex).count;
        if (!count || count > UINT32_MAX / sizeof(Vertex)) throw std::runtime_error(
            "Invalid mesh vertex count");
        std::vector<SourceVertex> vertices(count);
        auto vec3 = [&](std::string_view name, auto member)
        {
            return ReadAttribute<fastgltf::math::fvec3>(asset,
                                                        primitive,
                                                        name,
                                                        count,
                                                        fastgltf::AccessorType::Vec3,
                                                        [&](auto value, size_t i)
                                                        {
                                                            for (int j = 0; j < 3; ++j)
                                                                (vertices[i].*member)[j] = value[j];
                                                        });
        };
        vec3("POSITION", &SourceVertex::position);
        bool normals = vec3("NORMAL", &SourceVertex::normal);
        bool tangents = ReadAttribute<fastgltf::math::fvec4>(asset,
                                                             primitive,
                                                             "TANGENT",
                                                             count,
                                                             fastgltf::AccessorType::Vec4,
                                                             [&](auto value, size_t i)
                                                             {
                                                                 for (int j = 0; j < 4; ++j)
                                                                     vertices[i].tangent[j] = value[
                                                                         j];
                                                             });
        bool uv_present[2]{};
        for (int uv = 0; uv < 2; ++uv)
            uv_present[uv] = ReadAttribute<fastgltf::math::fvec2>(asset,
                primitive,
                uv ? "TEXCOORD_1" : "TEXCOORD_0",
                count,
                fastgltf::AccessorType::Vec2,
                [&](auto value, size_t i)
                {
                    for (int j = 0; j < 2; ++j) vertices[i].uv[uv][j] = value[j];
                });
        for (int slot = 0; slot < 5; ++slot)
            if (material.textures[slot] != UINT16_MAX && !uv_present[
                    (material.uv_sets >> slot) & 1])
                throw std::runtime_error("Material references missing texture coordinates");
        for (const auto& vertex : vertices) ValidateVertex(vertex);
        std::vector<uint32_t> indices;
        if (primitive.indicesAccessor)
        {
            const auto& accessor = asset.accessors.at(*primitive.indicesAccessor);
            if (accessor.type != fastgltf::AccessorType::Scalar || accessor.count > UINT32_MAX /
                sizeof(uint32_t) ||
                (accessor.componentType != fastgltf::ComponentType::UnsignedByte &&
                 accessor.componentType != fastgltf::ComponentType::UnsignedShort && accessor.
                 componentType != fastgltf::ComponentType::UnsignedInt))
                throw std::runtime_error("Invalid index accessor");
            indices.reserve(accessor.count);
            fastgltf::iterateAccessor<uint32_t>(asset,
                                                accessor,
                                                [&](uint32_t i) { indices.push_back(i); });
        }
        else
        {
            indices.resize(count);
            std::iota(indices.begin(), indices.end(), 0u);
        }
        if (indices.empty() || indices.size() % 3) throw std::runtime_error(
            "Invalid triangle index count");
        for (auto index : indices)
            if (index >= vertices.size()) throw std::runtime_error("Triangle index out of range");

        if (!normals || (!tangents && material.textures[2] != UINT16_MAX))
        {
            if (indices.size() > UINT32_MAX / sizeof(Vertex)) throw std::runtime_error(
                "Expanded mesh is too large");
            std::vector<SourceVertex> corners;
            corners.reserve(indices.size());
            for (auto i : indices) corners.push_back(vertices[i]);
            vertices = std::move(corners);
            std::iota(indices.begin(), indices.end(), 0u);
            if (!normals)
            {
                for (size_t i = 0; i < vertices.size(); i += 3)
                {
                    auto position_at = [&](size_t j)
                    {
                        const auto* p = vertices[j].position;
                        return glm::vec3(p[0], p[1], p[2]);
                    };
                    auto normal = glm::cross(position_at(i + 1) - position_at(i),
                                             position_at(i + 2) - position_at(i));
                    auto length = glm::length(normal);
                    normal = length > 0 ? normal / length : glm::vec3(0, 0, 1);
                    for (size_t j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
                        vertices[i + j].normal[k] = normal[k];
                }
            }
            if (!tangents && material.textures[2] != UINT16_MAX) GenerateTangents(
                vertices,
                (material.uv_sets >> 2) & 1);
        }
        std::vector<Vertex> packed;
        packed.reserve(vertices.size());
        for (const auto& vertex : vertices) packed.push_back(PackVertex(vertex));
        return Mesh(device, packed, indices, material_index, material.textures[0]);
    }

#ifdef RIMERA_MAC
    std::string ResourcePath()
    {
        NS::Bundle* bundle = NS::Bundle::mainBundle();
        NS::String* path = bundle->resourcePath();
        return std::string(path->utf8String());
    }
#endif
}


Model::Model(RM::Device* device, const std::filesystem::path& path)
{
    if (!device) throw std::runtime_error("Model requires a device");
    const std::filesystem::path portable_path = ResourcePath() + "/" + path.string();
    fastgltf::Parser parser;
    auto file = fastgltf::GltfDataBuffer::FromPath(portable_path);
    if (file.error() != fastgltf::Error::None) throw std::runtime_error(
        "Cannot open glTF: " + portable_path.string());
    auto parsed = parser.loadGltf(file.get(),
                                  portable_path.parent_path(),
                                  fastgltf::Options::LoadExternalBuffers |
                                  fastgltf::Options::LoadExternalImages);
    if (parsed.error() != fastgltf::Error::None)
        throw std::runtime_error(
            "glTF parse failed: " + std::string(fastgltf::getErrorMessage(parsed.error())));
    auto& asset = parsed.get();
    if (auto error = fastgltf::validate(asset); error != fastgltf::Error::None)
        throw std::runtime_error(
            "glTF validation failed: " + std::string(fastgltf::getErrorMessage(error)));
    if (!asset.extensionsRequired.empty()) throw std::runtime_error(
        "Required glTF extensions are not supported");
    if (!asset.skins.empty()) throw std::runtime_error("Skinned models are not supported");
    CheckData(asset);
    std::unordered_map<size_t, uint16_t> texture_map;
    auto load_texture = [&](size_t index, bool srgb) -> uint16_t
    {
        const auto key = index * 2 + size_t(srgb);
        if (auto found = texture_map.find(key); found != texture_map.end()) return found->second;
        if (m_textures.size() >= UINT16_MAX) throw std::runtime_error("Too many model textures");
        const auto& source = asset.textures.at(index);
        if (!source.imageIndex) throw std::runtime_error("Texture has no supported image source");
        TextureSampler sampler;
        if (source.samplerIndex)
        {
            const auto& s = asset.samplers.at(*source.samplerIndex);
            sampler.mag_filter = s.magFilter
                                     ? static_cast<TextureFilter>(*s.magFilter)
                                     : TextureFilter::Default;
            sampler.min_filter = s.minFilter
                                     ? static_cast<TextureFilter>(*s.minFilter)
                                     : TextureFilter::Default;
            sampler.wrap_s = static_cast<TextureWrap>(s.wrapS);
            sampler.wrap_t = static_cast<TextureWrap>(s.wrapT);
        }
        const auto& image = asset.images.at(*source.imageIndex);
        std::visit([&](const auto& data)
                   {
                       using T = std::decay_t<decltype(data)>;
                       if constexpr (std::is_same_v<T, fastgltf::sources::URI>)
                       {
                           if (!data.uri.isLocalPath() || data.fileByteOffset) throw
                               std::runtime_error(
                                   "Unsupported image URI");
                           m_textures.emplace_back(device,
                                                   portable_path.parent_path() / data.uri.fspath(),
                                                   srgb,
                                                   sampler);
                       }
                       else if constexpr (std::is_same_v<T, fastgltf::sources::BufferView>)
                       {
                           const auto& view = asset.bufferViews.at(data.bufferViewIndex);
                           auto bytes = Bytes(asset.buffers.at(view.bufferIndex).data).subspan(
                               view.byteOffset,
                               view.byteLength);
                           m_textures.emplace_back(device, bytes, srgb, sampler);
                       }
                       else if constexpr (requires { data.bytes; })
                           m_textures.emplace_back(device,
                                                   std::span<const std::byte>(
                                                       data.bytes.data(),
                                                       data.bytes.size()),
                                                   srgb,
                                                   sampler);
                       else throw std::runtime_error("Unsupported image source");
                   },
                   image.data);
        auto result = static_cast<uint16_t>(m_textures.size() - 1);
        texture_map.emplace(key, result);
        return result;
    };
    for (const auto& source : asset.materials)
    {
        Material material;
        for (int i = 0; i < 4; ++i) material.base_color[i] = source.pbrData.baseColorFactor[i];
        for (int i = 0; i < 3; ++i) material.emissive[i] = source.emissiveFactor[i];
        material.metallic = source.pbrData.metallicFactor;
        material.roughness = source.pbrData.roughnessFactor;
        material.alpha_cutoff = source.alphaCutoff;
        material.flags = (source.alphaMode == fastgltf::AlphaMode::Mask
                              ? 1
                              : source.alphaMode == fastgltf::AlphaMode::Blend
                              ? 2
                              : 0) | (source.doubleSided ? 4 : 0);
        auto slot = [&](const auto& info, unsigned slot_index, bool srgb)
        {
            if (!info) return;
            if (info->texCoordIndex > 1 || info->transform) throw std::runtime_error(
                "Unsupported texture coordinates or transform");
            material.textures[slot_index] = load_texture(info->textureIndex, srgb);
            material.uv_sets |= uint32_t(info->texCoordIndex) << slot_index;
        };
        slot(source.pbrData.baseColorTexture, 0, true);
        slot(source.pbrData.metallicRoughnessTexture, 1, false);
        slot(source.normalTexture, 2, false);
        slot(source.occlusionTexture, 3, false);
        slot(source.emissiveTexture, 4, true);
        if (source.normalTexture) material.normal_scale = source.normalTexture->scale;
        if (source.occlusionTexture) material.occlusion_strength = source.occlusionTexture->
                                     strength;
        m_materials.push_back(material);
    }
    const auto default_material = static_cast<uint32_t>(m_materials.size());
    m_materials.emplace_back();
    std::vector<std::vector<uint32_t>> mesh_map(asset.meshes.size());
    for (size_t i = 0; i < asset.meshes.size(); ++i)
    {
        for (const auto& primitive : asset.meshes[i].primitives)
        {
            auto material = primitive.materialIndex
                                ? static_cast<uint32_t>(*primitive.materialIndex)
                                : default_material;
            mesh_map[i].push_back(static_cast<uint32_t>(m_meshes.size()));
            m_meshes.push_back(LoadMesh(device,
                                        asset,
                                        primitive,
                                        material,
                                        m_materials.at(material)));
        }
    }
    m_nodes.resize(asset.nodes.size());
    for (size_t i = 0; i < asset.nodes.size(); ++i)
    {
        const auto& source = asset.nodes[i];
        auto& node = m_nodes[i];
        if (source.skinIndex || !source.weights.empty()) throw std::runtime_error(
            "Deformable nodes are not supported");
        node.name = source.name;
        auto matrix = fastgltf::getTransformMatrix(source);
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            node.local_transform[c][r] = matrix[c][r];
        if (source.meshIndex) node.meshes = mesh_map.at(*source.meshIndex);
        for (auto child : source.children)
        {
            auto& child_node = m_nodes.at(child);
            if (child_node.parent != UINT32_MAX) throw std::runtime_error(
                "Node has multiple parents");
            child_node.parent = static_cast<uint32_t>(i);
            node.children.push_back(static_cast<uint32_t>(child));
        }
    }
    std::vector<uint32_t> pending;
    for (size_t i = 0; i < m_nodes.size(); ++i) if (m_nodes[i].parent == UINT32_MAX) pending.
        push_back(static_cast<uint32_t>(i));
    auto all_roots = pending;
    size_t visited = 0;
    while (!pending.empty())
    {
        auto index = pending.back();
        pending.pop_back();
        ++visited;
        auto& node = m_nodes[index];
        node.world_transform = node.parent == UINT32_MAX
                                   ? node.local_transform
                                   : m_nodes[node.parent].world_transform * node.local_transform;
        pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
    if (visited != m_nodes.size()) throw std::runtime_error("Cyclic node hierarchy");
    if (!asset.scenes.empty())
    {
        const auto& scene = asset.scenes.at(asset.defaultScene.value_or(0));
        for (auto root : scene.nodeIndices)
        {
            if (m_nodes.at(root).parent != UINT32_MAX) throw std::runtime_error(
                "Scene root has a parent");
            m_scene_roots.push_back(static_cast<uint32_t>(root));
        }
    }
    else m_scene_roots = std::move(all_roots);
}