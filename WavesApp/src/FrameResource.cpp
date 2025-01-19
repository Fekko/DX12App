#include "FrameResource.h"


FrameResource::FrameResource(ID3D12Device* pDevice, UINT passCount, UINT objectCount, UINT waveVertCount) {
	THROW_IF_FAILED(pDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())
	));

	PassCBuffer = std::make_unique<UploadBuffer<PassConstants>>(pDevice, passCount, true);
	ObjectCBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(pDevice, objectCount, true);
	WavesVertexBuffer = std::make_unique<UploadBuffer<Vertex>>(pDevice, waveVertCount, false);
}