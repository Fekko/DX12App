#pragma once
#include <memory>

#include "DxUtil.h"
#include "Vertex.h"

class FrameResource
{
	FrameResource(ID3D12Device* pDevice);
	~FrameResource() {};

	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator&(const FrameResource& rhs) = delete;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc{};

	UINT64 Fence{};
};