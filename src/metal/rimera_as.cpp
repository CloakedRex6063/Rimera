#include "rimera_as.hpp"

#include "rimera_buffer.hpp"
#include "rimera_device.hpp"

RM::BLAS::BLAS(Device* device, const BLASCreateInfo& info)
{
    auto prim_desc = NS::TransferPtr(MTL4::PrimitiveAccelerationStructureDescriptor::alloc()->init());
    std::vector<MTL4::AccelerationStructureTriangleGeometryDescriptor*> geom_descs(info.infos.size());
    for (int i = 0; i < info.infos.size(); i++)
    {
        auto& geom_info = info.infos[i];
        auto* vertex_buffer = geom_info.vertex_buffer;
        auto* index_buffer = geom_info.index_buffer;
        geom_descs[i] = MTL4::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
        geom_descs[i]->setVertexBuffer(MTL4::BufferRange(vertex_buffer->GetGPUAddress(), vertex_buffer->GetLength()));
        geom_descs[i]->setVertexFormat(MTL::AttributeFormatFloat3); // Handle different vertex formats later
        geom_descs[i]->setVertexStride(geom_info.vertex_stride);
        geom_descs[i]->setIndexBuffer(MTL4::BufferRange(index_buffer->GetGPUAddress(), index_buffer->GetLength()));
        geom_descs[i]->setIndexType(MTL::IndexTypeUInt32);
        geom_descs[i]->setTriangleCount(geom_info.triangle_count);

        NS::Array* geom_descs_arr = NS::Array::array(reinterpret_cast<NS::Object**>(geom_descs.data()), geom_descs.size());
        prim_desc->setGeometryDescriptors(geom_descs_arr);
    }
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    m_blas = NS::TransferPtr(native_device->newAccelerationStructure(prim_desc.get()));
    
    const auto sizes = native_device->accelerationStructureSizes(prim_desc.get());
    const auto scratch_buffer =
        NS::TransferPtr(native_device->newBuffer(sizes.buildScratchBufferSize, MTL::ResourceStorageModeShared));
    
    auto* command = device->CreateCommand(CommandType::Compute);
    auto* native_command = static_cast<MTL4::CommandBuffer*>(command->GetNativeCommand());
    
    auto* compute_encoder = native_command->computeCommandEncoder();
    compute_encoder->buildAccelerationStructure(m_blas.get(), prim_desc.get(), MTL4::BufferRange(scratch_buffer->gpuAddress()));
    compute_encoder->endEncoding();
    
    auto* queue = device->CreateQueue(QueueType::Compute);
    queue->Submit(command);
    queue->WaitIdle();
    
    device->DestroyQueue(queue);
    device->DestroyCommand(command);
    
    for (int i = 0; i < info.infos.size(); i++)
    {
        geom_descs[i]->release();
    }
}

void* RM::BLAS::GetNativeHandle() const
{
    return m_blas.get();
}

void* RM::TLAS::GetNativeHandle() const
{
    return m_tlas.get();
}

static MTL::PackedFloat4x3 ToMetalTransform(const RM::Float4x4& m)
{
    return MTL::PackedFloat4x3(MTL::PackedFloat3(m[0][0], m[0][1], m[0][2]),
                               MTL::PackedFloat3(m[1][0], m[1][1], m[1][2]),
                               MTL::PackedFloat3(m[2][0], m[2][1], m[2][2]),
                               MTL::PackedFloat3(m[3][0], m[3][1], m[3][2]));
}

RM::TLAS::TLAS(Device* device, const TLASCreateInfo& info)
{
    const auto instance_desc = NS::TransferPtr(MTL4::InstanceAccelerationStructureDescriptor::alloc()->init());
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    instance_desc->setInstanceCount(info.infos.size());
    instance_desc->setInstanceDescriptorType(MTL::AccelerationStructureInstanceDescriptorTypeIndirect);
    instance_desc->setInstanceDescriptorStride(sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor));
    
    const auto sizes = native_device->accelerationStructureSizes(instance_desc.get());
    const auto data_buffer_size = sizes.accelerationStructureSize + sizes.buildScratchBufferSize;
    scratch_offset = sizes.accelerationStructureSize;
    m_data_buffer = NS::TransferPtr(native_device->newBuffer(data_buffer_size, MTL::ResourceStorageModeShared));
    
    auto* instance_descriptors =
    static_cast<MTL::IndirectAccelerationStructureInstanceDescriptor*>(m_data_buffer->contents());
    for (int i = 0; i < info.infos.size(); i++)
    {
        const TLASInstanceCreateInfo& descriptor = info.infos[i];
        const auto* blas = static_cast<MTL::AccelerationStructure*>(descriptor.blas->GetNativeHandle());
        instance_descriptors[i] = MTL::IndirectAccelerationStructureInstanceDescriptor{
            .transformationMatrix = ToMetalTransform(descriptor.transform),
            .options = MTL::AccelerationStructureInstanceOptionNone,
            .mask = 0xFF,
            .userID = descriptor.user_id,
            .accelerationStructureID = blas->gpuResourceID(),
        };
    }
    
    instance_desc->setInstanceDescriptorBuffer(MTL4::BufferRange(m_data_buffer->gpuAddress(), scratch_offset));
    m_tlas = NS::TransferPtr(native_device->newAccelerationStructure(instance_desc.get()));

    const auto* command = device->CreateCommand(CommandType::Compute);
    
    auto* native_command = static_cast<MTL4::CommandBuffer*>(command->GetNativeCommand());
    
    auto* compute_encoder = native_command->computeCommandEncoder();
    compute_encoder->buildAccelerationStructure(m_tlas.get(), instance_desc.get(), MTL4::BufferRange(m_data_buffer->gpuAddress() + scratch_offset, sizes.buildScratchBufferSize));
    compute_encoder->endEncoding();
    
    auto* queue = device->CreateQueue(QueueType::Compute);
    queue->Submit(command);
    queue->WaitIdle();
    
    device->DestroyCommand(command);
}