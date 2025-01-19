#pragma once
#include <memory>

#include "DxUtil.h"
#include "UploadBuffer.h"  
#include "MathHelper.h"

struct PassConstants{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosition = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;
};

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
};

struct FrameResource
{
	FrameResource(ID3D12Device* pDevice, UINT passCount, UINT objectCount);
	~FrameResource() {};

	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator&(const FrameResource& rhs) = delete;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc{};
	std::unique_ptr<UploadBuffer<PassConstants>> PassCBuffer{};
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCBuffer{};

	UINT64 Fence{};
};