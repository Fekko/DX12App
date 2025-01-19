#pragma once

#include "MathHelper.h"
#include "MeshGeometry.h"

struct RenderItem
{
	RenderItem() = default;

	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	const static int NrFrameResources{ 3 };
	int NrFramesDirty{ NrFrameResources };

	UINT ObjectCBufferIndex{ UINT_MAX };
	MeshGeometry* pMeshGeometry{};
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	UINT IndexCount{};
	UINT StartIndexLocation{};
	int BaseVertexLocation{};
};

