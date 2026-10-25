#include "model.hpp"
#include "fileio.hpp"
#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"
#include "mikktspace.h"
#include "meshoptimizer.h"
#include "bit"
#include "algorithm"
#include "numeric"
#include "unordered_map"

RM::Result<std::span<const std::byte>> Model::Bytes(const fastgltf::DataSource& source)
{
    return std::visit(
        [](const auto& data) -> RM::Result<std::span<const std::byte>>
        {
            if constexpr (requires { data.bytes; })
                return std::span<const std::byte>{data.bytes.data(), data.bytes.size()};
            else
                return std::unexpected("Unsupported glTF buffer source");
        },
        source);
}

RM::Result<void> Model::CheckData(const fastgltf::Asset& asset)
{
    for (const auto& view : asset.bufferViews)
    {
        auto result_bytes = Bytes(asset.buffers[view.bufferIndex].data);
        if (!result_bytes) return std::unexpected(result_bytes.error());
        auto bytes = std::move(*result_bytes);
        if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset)
            return std::unexpected("glTF buffer view exceeds buffer");
    }
    auto check = [&](size_t view_index,
                     size_t offset,
                     size_t count,
                     size_t element_size,
                     size_t stride) -> RM::Result<void>
    {
        const auto& view = asset.bufferViews[view_index];
        if (!stride || stride < element_size || offset > view.byteLength ||
            (count && (element_size > view.byteLength - offset ||
                       count - 1 > (view.byteLength - offset - element_size) / stride)))
            return std::unexpected("glTF accessor exceeds buffer view");
        return {};
    };
    for (const auto& accessor : asset.accessors)
    {
        auto element_size = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
        if (!element_size) return std::unexpected("Invalid glTF accessor element size");
        if (accessor.bufferViewIndex)
        {
            const auto& view = asset.bufferViews[*accessor.bufferViewIndex];
            if (auto result = check(*accessor.bufferViewIndex,
                                    accessor.byteOffset,
                                    accessor.count,
                                    element_size,
                                    view.byteStride.value_or(element_size));
                !result)
                return std::unexpected(result.error());
        }
        if (accessor.sparse)
        {
            const auto& sparse = *accessor.sparse;
            if (sparse.count > accessor.count ||
                (sparse.indexComponentType != fastgltf::ComponentType::UnsignedByte &&
                 sparse.indexComponentType != fastgltf::ComponentType::UnsignedShort &&
                 sparse.indexComponentType != fastgltf::ComponentType::UnsignedInt))
                return std::unexpected("Invalid sparse accessor indices");
            auto index_size = fastgltf::getComponentByteSize(sparse.indexComponentType);
            if (auto result = check(sparse.indicesBufferView,
                                    sparse.indicesByteOffset,
                                    sparse.count,
                                    index_size,
                                    index_size);
                !result)
                return std::unexpected(result.error());
            if (auto result = check(sparse.valuesBufferView,
                                    sparse.valuesByteOffset,
                                    sparse.count,
                                    element_size,
                                    element_size);
                !result)
                return std::unexpected(result.error());
            const auto& index_view = asset.bufferViews[sparse.indicesBufferView];
            auto result_bytes = Bytes(asset.buffers[index_view.bufferIndex].data);
            if (!result_bytes) return std::unexpected(result_bytes.error());
            auto bytes = std::move(*result_bytes);
            uint32_t previous = 0;
            for (size_t i = 0; i < sparse.count; ++i)
            {
                uint32_t index = 0;
                std::memcpy(&index,
                            bytes.data() + index_view.byteOffset + sparse.indicesByteOffset + i
                            * index_size,
                            index_size);
                if (index >= accessor.count || (i && index <= previous))
                    return std::unexpected("Sparse indices must be ordered and in range");
                previous = index;
            }
        }
    }
    return {};
}

template <class T, class F>
RM::Result<bool> Model::ReadAttribute(const fastgltf::Asset& asset,
                               const fastgltf::Primitive& primitive,
                               std::string_view name,
                               size_t count,
                               fastgltf::AccessorType type,
                               F write)
{
    auto it = primitive.findAttribute(name);
    if (it == primitive.attributes.end()) return false;
    const auto& accessor = asset.accessors[it->accessorIndex];
    if (accessor.count != count || accessor.type != type)
        return std::unexpected("Invalid glTF attribute: " + std::string(name));
    fastgltf::iterateAccessorWithIndex<T>(asset,
                                          accessor,
                                          [&](T value, size_t i) { write(value, i); });
    return true;
}

// For normal/tangent generation
struct Model::SourceVertex
{
    float position[3]{};
    float normal[3]{};
    float tangent[4]{1, 0, 0, 1};
    float uv[2]{};
};

RM::Result<void> Model::ValidateVertex(const SourceVertex& vertex)
{
    auto finite = [](const auto& values) -> RM::Result<void>
    {
        for (float value : values)
            if (!std::isfinite(value)) return std::unexpected("Non-finite vertex attribute");
        return {};
    };
    if (auto result = finite(vertex.position); !result) return std::unexpected(result.error());
    if (auto result = finite(vertex.normal); !result) return std::unexpected(result.error());
    if (auto result = finite(vertex.tangent); !result) return std::unexpected(result.error());
    if (auto result = finite(vertex.uv); !result) return std::unexpected(result.error());
    for (float value : vertex.uv)
        if (std::abs(value) > 65504.f) return std::unexpected("UV exceeds finite half-float range");
    return {};
}

uint32_t Model::PackDirection(const float* direction, glm::vec3 fallback)
{
    glm::vec3 unit(direction[0], direction[1], direction[2]);
    float scale = std::max({std::abs(unit.x), std::abs(unit.y), std::abs(unit.z)});
    unit = scale > 0 ? glm::normalize(unit / scale) : fallback;
    uint32_t bits = 0;
    for (unsigned i = 0; i < 3; ++i)
        bits |= (static_cast<uint32_t>(meshopt_quantizeSnorm(unit[i], 10)) & 0x3ffu) << (
            i * 10);
    return bits;
}

RM::Result<Vertex> Model::PackVertex(const SourceVertex& source)
{
    if (auto result = ValidateVertex(source); !result) return std::unexpected(result.error());
    Vertex result{};
    for (unsigned i = 0; i < 3; ++i) result.position[i] = source.position[i];
    result.normal = PackDirection(source.normal, {0, 0, 1});
    result.tangent = PackDirection(source.tangent, {1, 0, 0}) |
                     ((source.tangent[3] < 0 ? 3u : 1u) << 30);
    result.uv = static_cast<uint32_t>(meshopt_quantizeHalf(source.uv[0])) |
                (static_cast<uint32_t>(meshopt_quantizeHalf(source.uv[1])) << 16);
    return result;
}

struct Model::TangentData
{
    std::vector<SourceVertex>* vertices;
};

Model::SourceVertex& Model::Corner(const SMikkTSpaceContext* context, int face, int vertex)
{
    return (*static_cast<TangentData*>(context->m_pUserData)->vertices)[
        static_cast<size_t>(face) * 3 + vertex];
}

RM::Result<void> Model::GenerateTangents(std::vector<SourceVertex>& vertices)
{
    TangentData data{&vertices};
    SMikkTSpaceInterface mikkspace{};
    mikkspace.m_getNumFaces = [](const SMikkTSpaceContext* c)
    {
        return static_cast<int>(static_cast<TangentData*>(c->m_pUserData)->vertices->size() /
                                3);
    };
    mikkspace.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
    mikkspace.m_getPosition = [](const SMikkTSpaceContext* c, float* out, int f, int v)
    {
        std::copy_n(Corner(c, f, v).position, 3, out);
    };
    mikkspace.m_getNormal = [](const SMikkTSpaceContext* c, float* out, int f, int v)
    {
        std::copy_n(Corner(c, f, v).normal, 3, out);
    };
    mikkspace.m_getTexCoord = [](const SMikkTSpaceContext* c, float* out, int f, int v)
    {
        std::copy_n(Corner(c, f, v).uv, 2, out);
    };
    mikkspace.m_setTSpaceBasic = [](const SMikkTSpaceContext* c,
                                    const float* tangent,
                                    float sign,
                                    int f,
                                    int v)
    {
        auto& vertex = Corner(c, f, v);
        std::copy_n(tangent, 3, vertex.tangent);
        vertex.tangent[3] = sign;
    };
    SMikkTSpaceContext context{&mikkspace, &data};
    if (!genTangSpaceDefault(&context)) return std::unexpected("Tangent generation failed");
    return {};
}

RM::Result<Mesh*> Model::LoadMesh(RM::Device* device,
                           const fastgltf::Asset& asset,
                           const fastgltf::Primitive& primitive,
                           uint32_t material_index,
                           const Material& material)
{
    if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty() ||
        primitive.dracoCompression)
        return std::unexpected("Only uncompressed static triangle primitives are supported");
    auto position = primitive.findAttribute("POSITION");
    if (position == primitive.attributes.end())
        return std::unexpected("Mesh has no POSITION attribute");
    size_t count = asset.accessors[position->accessorIndex].count;
    if (!count || count > UINT32_MAX / sizeof(Vertex))
        return std::unexpected("Invalid mesh vertex count");
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
    if (auto result = vec3("POSITION", &SourceVertex::position); !result)
        return std::unexpected(result.error());
    auto normals_result = vec3("NORMAL", &SourceVertex::normal);
    if (!normals_result) return std::unexpected(normals_result.error());
    bool normals = *normals_result;
    auto tangents_result = ReadAttribute<fastgltf::math::fvec4>(asset,
                                                                primitive,
                                                                "TANGENT",
                                                                count,
                                                                fastgltf::AccessorType::Vec4,
                                                                [&](auto value, size_t i)
                                                                {
                                                                    for (int j = 0; j < 4; ++j)
                                                                        vertices[i].tangent[j] =
                                                                            value[j];
                                                                });
    if (!tangents_result) return std::unexpected(tangents_result.error());
    bool tangents = *tangents_result;
    auto uv_result = ReadAttribute<fastgltf::math::fvec2>(asset,
                                                          primitive,
                                                          "TEXCOORD_0",
                                                          count,
                                                          fastgltf::AccessorType::Vec2,
                                                          [&](auto value, size_t i)
                                                          {
                                                              for (int j = 0; j < 2; ++j)
                                                                  vertices[i].uv[j] = value[j];
                                                          });
    if (!uv_result) return std::unexpected(uv_result.error());
    bool uv_present = *uv_result;
    for (int slot = 0; slot < 5; ++slot)
        if (material.textures[slot] != UINT16_MAX && !uv_present)
            return std::unexpected("Material references missing texture coordinates");
    for (const auto& vertex : vertices)
    {
        if (auto result = ValidateVertex(vertex); !result)
            return std::unexpected(result.error());
    }
    std::vector<uint32_t> indices;
    if (primitive.indicesAccessor)
    {
        const auto& accessor = asset.accessors[*primitive.indicesAccessor];
        if (accessor.type != fastgltf::AccessorType::Scalar || accessor.count > UINT32_MAX /
            sizeof(uint32_t) ||
            (accessor.componentType != fastgltf::ComponentType::UnsignedByte &&
             accessor.componentType != fastgltf::ComponentType::UnsignedShort && accessor.
             componentType != fastgltf::ComponentType::UnsignedInt))
            return std::unexpected("Invalid index accessor");
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
    if (indices.empty() || indices.size() % 3)
        return std::unexpected("Invalid triangle index count");
    for (auto index : indices)
        if (index >= vertices.size()) return std::unexpected("Triangle index out of range");

    if (!normals || (!tangents && material.textures[2] != UINT16_MAX))
    {
        if (indices.size() > UINT32_MAX / sizeof(Vertex))
            return std::unexpected("Expanded mesh is too large");
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
                for (size_t j = 0; j < 3; ++j)
                    for (int k = 0; k < 3; ++k)
                        vertices[i + j].normal[k] = normal[k];
            }
        }
        if (!tangents && material.textures[2] != UINT16_MAX)
            if (auto result = GenerateTangents(vertices); !result)
                return std::unexpected(result.error());
    }
    std::vector<Vertex> packed;
    packed.reserve(vertices.size());
    for (const auto& vertex : vertices)
    {
        auto result = PackVertex(vertex);
        if (!result) return std::unexpected(result.error());
        packed.push_back(*result);
    }
    return Mesh::Create(device, packed, indices, material_index, material.textures[0]);
}

RM::Result<Model*> Model::Create(RM::Device* device, const std::filesystem::path& path)
{
    auto* object = new (std::nothrow) Model();
    if (!object) return std::unexpected("Object allocation failed");
    auto result = object->Initialize(device, path);
    if (!result)
    {
        auto error = std::move(result.error());
        delete object;
        return std::unexpected(std::move(error));
    }
    return object;
}

RM::Result<void> Model::Initialize(RM::Device* device, const std::filesystem::path& path)
{
    if (!device) return std::unexpected("Model requires a device");
    const std::filesystem::path portable_path = FileIO::ResourcePath() + path.string();
    fastgltf::Parser parser;
    auto file = fastgltf::GltfDataBuffer::FromPath(portable_path);
    if (file.error() != fastgltf::Error::None)
        return std::unexpected("Cannot open glTF: " + portable_path.string());
    auto parsed = parser.loadGltf(file.get(),
                                  portable_path.parent_path(),
                                  fastgltf::Options::LoadExternalBuffers |
                                  fastgltf::Options::LoadExternalImages);
    if (parsed.error() != fastgltf::Error::None)
        return std::unexpected("glTF parse failed: " +
                               std::string(fastgltf::getErrorMessage(parsed.error())));
    auto& asset = parsed.get();
    if (auto error = fastgltf::validate(asset); error != fastgltf::Error::None)
        return std::unexpected("glTF validation failed: " +
                               std::string(fastgltf::getErrorMessage(error)));
    if (!asset.extensionsRequired.empty())
        return std::unexpected("Required glTF extensions are not supported");
    if (!asset.skins.empty()) return std::unexpected("Skinned models are not supported");
    if (auto result = CheckData(asset); !result) return std::unexpected(result.error());
    std::unordered_map<size_t, uint16_t> texture_map;
    auto load_texture = [&](size_t index, bool srgb) -> RM::Result<uint16_t>
    {
        const auto key = index * 2 + static_cast<size_t>(srgb);
        if (auto found = texture_map.find(key); found != texture_map.end()) return found->second;
        if (m_textures.size() >= UINT16_MAX) return std::unexpected("Too many model textures");
        const auto& source = asset.textures[index];
        if (!source.imageIndex) return std::unexpected("Texture has no supported image source");
        TextureSampler sampler;
        if (source.samplerIndex)
        {
            const auto& s = asset.samplers[*source.samplerIndex];
            sampler.mag_filter = s.magFilter
                                     ? static_cast<TextureFilter>(*s.magFilter)
                                     : TextureFilter::Default;
            sampler.min_filter = s.minFilter
                                     ? static_cast<TextureFilter>(*s.minFilter)
                                     : TextureFilter::Default;
            sampler.wrap_s = static_cast<TextureWrap>(s.wrapS);
            sampler.wrap_t = static_cast<TextureWrap>(s.wrapT);
        }
        const auto& image = asset.images[*source.imageIndex];
        auto texture = std::visit(
            [&](const auto& data) -> RM::Result<Texture*>
            {
                using T = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<T, fastgltf::sources::URI>)
                {
                    if (!data.uri.isLocalPath() || data.fileByteOffset)
                        return std::unexpected("Unsupported image URI");
                    return Texture::Create(device,
                                           portable_path.parent_path() / data.uri.fspath(),
                                           srgb,
                                           sampler);
                }
                else if constexpr (std::is_same_v<T, fastgltf::sources::BufferView>)
                {
                    const auto& view = asset.bufferViews[data.bufferViewIndex];
                    auto bytes = Bytes(asset.buffers[view.bufferIndex].data);
                    if (!bytes) return std::unexpected(bytes.error());
                    return Texture::Create(device,
                                           bytes->subspan(view.byteOffset, view.byteLength),
                                           srgb,
                                           sampler);
                }
                else if constexpr (requires { data.bytes; })
                    return Texture::Create(
                        device,
                        std::span<const std::byte>(data.bytes.data(), data.bytes.size()),
                        srgb,
                        sampler);
                else
                    return std::unexpected("Unsupported image source");
            },
            image.data);
        if (!texture) return std::unexpected(texture.error());
        m_textures.push_back(*texture);
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
        auto slot = [&](const auto& info, unsigned slot_index, bool srgb) -> RM::Result<void>
        {
            if (!info) return {};
            if (info->texCoordIndex != 0 || info->transform)
                return std::unexpected("Unsupported texture coordinates or transform");
            auto texture_result = load_texture(info->textureIndex, srgb);
            if (!texture_result) return std::unexpected(texture_result.error());
            material.textures[slot_index] = std::move(*texture_result);
            return {};
        };
        if (auto result = slot(source.pbrData.baseColorTexture, 0, true); !result)
            return std::unexpected(result.error());
        if (auto result = slot(source.pbrData.metallicRoughnessTexture, 1, false); !result)
            return std::unexpected(result.error());
        if (auto result = slot(source.normalTexture, 2, false); !result)
            return std::unexpected(result.error());
        if (auto result = slot(source.occlusionTexture, 3, false); !result)
            return std::unexpected(result.error());
        if (auto result = slot(source.emissiveTexture, 4, true); !result)
            return std::unexpected(result.error());
        if (source.normalTexture) material.normal_scale = source.normalTexture->scale;
        if (source.occlusionTexture)
            material.occlusion_strength = source.occlusionTexture->
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
            auto mesh = LoadMesh(device, asset, primitive, material, m_materials[material]);
            if (!mesh) return std::unexpected(mesh.error());
            m_meshes.push_back(*mesh);
        }
    }
    m_nodes.resize(asset.nodes.size());
    for (size_t i = 0; i < asset.nodes.size(); ++i)
    {
        const auto& source = asset.nodes[i];
        auto& node = m_nodes[i];
        if (source.skinIndex || !source.weights.empty())
            return std::unexpected("Deformable nodes are not supported");
        node.name = source.name;
        auto matrix = fastgltf::getTransformMatrix(source);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                node.local_transform[c][r] = matrix[c][r];
        if (source.meshIndex) node.meshes = mesh_map[*source.meshIndex];
        for (auto child : source.children)
        {
            auto& child_node = m_nodes[child];
            if (child_node.parent != UINT32_MAX)
                return std::unexpected("Node has multiple parents");
            child_node.parent = static_cast<uint32_t>(i);
            node.children.push_back(static_cast<uint32_t>(child));
        }
    }
    std::vector<uint32_t> pending;
    for (size_t i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].parent == UINT32_MAX)
            pending.
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
    if (visited != m_nodes.size()) return std::unexpected("Cyclic node hierarchy");
    if (!asset.scenes.empty())
    {
        const auto& scene = asset.scenes[asset.defaultScene.value_or(0)];
        for (auto root : scene.nodeIndices)
        {
            if (m_nodes[root].parent != UINT32_MAX)
                return std::unexpected("Scene root has a parent");
            m_scene_roots.push_back(static_cast<uint32_t>(root));
        }
    }
    else m_scene_roots = std::move(all_roots);
    return {};
}

Model::~Model()
{
    for (auto* mesh : m_meshes) delete mesh;
    for (auto* texture : m_textures) delete texture;
}
