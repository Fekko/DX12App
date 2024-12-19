#include "MeshGeometry.h"

D3D12_VERTEX_BUFFER_VIEW MeshGeometry::VertexBufferView() const {
    return D3D12_VERTEX_BUFFER_VIEW{
        .BufferLocation = VertexBufferGpu->GetGPUVirtualAddress(),
        .SizeInBytes = VertexBufferByteSize,
        .StrideInBytes = VertexByteStride,
    };
}

D3D12_INDEX_BUFFER_VIEW MeshGeometry::IndexBufferView() const {
    return D3D12_INDEX_BUFFER_VIEW{
        .BufferLocation = IndexBufferGpu->GetGPUVirtualAddress(),
        .SizeInBytes = IndexBufferByteSize,
        .Format = IndexFormat,
    };
}
