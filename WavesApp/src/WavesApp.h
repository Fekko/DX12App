#pragma once

#include <memory>
#include <vector>

#include "RenderItem.h"
#include "FrameResource.h"
#include "DxUtil.h"
#include "App.h"
#include "UploadBuffer.h"
#include "MathHelper.h"
#include "MeshGeometry.h"
#include "Waves.h"

class WavesApp final : public App
{
public:
	WavesApp(HINSTANCE hInstance);
	~WavesApp() noexcept;

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void BuildRootSignature();
	void BuildShaders();
	void BuildInputLayout();
	void BuildLandGeometry();
	void BuildWavesGeometryBuffers();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();

	void DrawRenderItems(ID3D12GraphicsCommandList* commandList, const std::vector<RenderItem*>& renderItems);

	float GetHillsHeight(float x, float z) const;
	DirectX::XMFLOAT3 GetHillsNormal(float x, float z) const;

private:
	std::vector<std::unique_ptr<FrameResource>> _frameResources{};
	FrameResource* _pCurrentFrameResource{};
	int _currentFrameResourceIndex{};
	UINT _cbvSrvDescriptorSize{};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _pRootSignature{};

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _srvDescriptorHeap{};

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> _geometries{};
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> _shaders{};
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> _pipelineStateObjects{};

	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout{};

	std::unique_ptr<Waves> _pWaves;
	RenderItem* _pWavesRenderItem{};

	std::vector<std::unique_ptr<RenderItem>> _renderItems{};
	std::vector<RenderItem*> _opaqueRenderItems{};

	PassConstants _mainPassCB{};
	bool _isWireframe{ false };

	DirectX::XMFLOAT3 _eyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 _view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _projection = MathHelper::Identity4x4();

	float _theta{ 1.5f * DirectX::XM_PI };
	float _phi{ DirectX::XM_PIDIV2 -.1f };
	float _radius{ 50.f };

	POINT _lastMousePosition{};
};