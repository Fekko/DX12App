#pragma once

#include <memory>
#include <vector>

#include "DxUtil.h"
#include "App.h"
#include "UploadBuffer.h"
#include "MathHelper.h"
#include "MeshGeometry.h"

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

class BoxApp final : public App
{
public:
	BoxApp(HINSTANCE hInstance);
	~BoxApp() noexcept;

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShaders();
	void BuildInputLayout();
	void BuildBoxGeometry();
	void BuildPipelineStateObject(); // PSO

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _pRootSignature{};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _pCbvHeap{}; // Constant buffer heap

	std::unique_ptr<UploadBuffer<ObjectConstants>> _pUploadBuffer{};
	std::unique_ptr<MeshGeometry> _pMeshGeometry{};

	Microsoft::WRL::ComPtr<ID3DBlob> _vertexShader{};
	Microsoft::WRL::ComPtr<ID3DBlob> _pixelShader{};

	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pPipelineStateObject{}; // PSO

	// TODO: NAMING CONVENTION
	DirectX::XMFLOAT4X4 _world = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _projection = MathHelper::Identity4x4();

	float _theta{ 1.5f * DirectX::XM_PI };
	float _phi{ DirectX::XM_PIDIV4 };
	float _radius{ 5.f };

	POINT _lastMousePosition{};
};