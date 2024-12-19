#pragma once

#include <string>
#include <unordered_map>

#include "Dxutil.h"

struct SubMeshGeometry
{
	UINT IndexCount{};
	UINT StartIndexLocation{};
	INT BaseVertexLocation{};
	DirectX::BoundingBox BoundingBox{};
};

struct MeshGeometry final
{
	std::string Name;

	Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCpu{};
	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGpu{};
	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader{};

	Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCpu{};
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGpu{};
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader{};

	UINT VertexByteStride{};
	UINT VertexBufferByteSize{};
	UINT IndexBufferByteSize{};
	DXGI_FORMAT IndexFormat{ DXGI_FORMAT_R16_UINT };

	std::unordered_map<std::string, SubMeshGeometry> DrawArguments{};

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const;
};

