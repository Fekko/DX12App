#pragma once

#include "App.h"
#include <MathHelper.h>

class DemoApp final : public App
{
public:
	DemoApp(HINSTANCE hInstance);
	~DemoApp() noexcept;

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	void BuildRootSignature();
	void BuildShaders();
	void BuildInputLayout();
	void BuildGeometry();
	void BuildRenderItems();
	void BuildFrameResources();
	void BuildPSOs();

private:
	DirectX::XMFLOAT3 _eyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 _view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 _projection = MathHelper::Identity4x4();


	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pPipelineStateObject{};
};