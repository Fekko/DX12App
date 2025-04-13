#include "DemoApp.h"

#include <DirectXColors.h>

using namespace DirectX;
using namespace Microsoft::WRL;

DemoApp::DemoApp(HINSTANCE hInstance)
	: App(hInstance) 
{
	_title = L"DemoApp";  
}

DemoApp::~DemoApp() {
	if (_pDevice) FlushCommandQueue();
} 

bool DemoApp::Initialize() {
	if (not App::Initialize()) return false;

	THROW_IF_FAILED(
		_pCommandList->Reset(_pCommandAllocator.Get(), nullptr));

	// INIT THINGS
	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	THROW_IF_FAILED(_pCommandList->Close());
	ID3D12CommandList* pCommandLists[] = { _pCommandList.Get() };
	_pCommandQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);
	FlushCommandQueue();
	return true;
}

void DemoApp::OnResize() {
	App::OnResize();

	// TODO: Def somewhere else
	float nearPlane = 1;
	float farPlane = 1000;

	// Recalculate aspect ration and projection matrix
	XMMATRIX projection = XMMatrixPerspectiveFovLH(
		XM_PIDIV4,
		AspectRatio(),
		nearPlane, farPlane);

	XMStoreFloat4x4(&_projection, projection);
}

void DemoApp::Update(const GameTimer& gt) {

}

void DemoApp::Draw(const GameTimer& gt) {
	THROW_IF_FAILED(_pCommandAllocator->Reset());
	THROW_IF_FAILED(_pCommandList->Reset(_pCommandAllocator.Get(), _pPipelineStateObject.Get()));

	_pCommandList->RSSetViewports(1, &_screenViewport);
	_pCommandList->RSSetScissorRects(1, &_scissorRect);

	auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_pCommandList->ResourceBarrier(1, &barrier1);

	_pCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	_pCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	auto currentBackBufferView = CurrentBackBufferView();
	auto depthStencilView = DepthStencilView();

	// OM = Output Merger stage
	_pCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

	ID3D12DescriptorHeap* descriptorHeaps[] = { _pCbvHeap.Get() };
	_pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	_pCommandList->SetGraphicsRootSignature(_pRootSignature.Get());

	auto iBufferView = _pMeshGeometry->IndexBufferView();
	auto vBufferView = _pMeshGeometry->VertexBufferView();
	_pCommandList->IASetIndexBuffer(&iBufferView);
	_pCommandList->IASetVertexBuffers(0, 1, &vBufferView);

	_pCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_pCommandList->SetGraphicsRootDescriptorTable(0, _pCbvHeap->GetGPUDescriptorHandleForHeapStart());
	_pCommandList->DrawIndexedInstanced(_pMeshGeometry->DrawArguments["box"].IndexCount,
		1, 0, 0, 0);

	// Indicate a state transition on the resource usage.
	auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	_pCommandList->ResourceBarrier(1, &barrier2);

	// Done recording commands.
	THROW_IF_FAILED(_pCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { _pCommandList.Get() };
	_pCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// swap the back and front buffers
	THROW_IF_FAILED(_pSwapChain->Present(0, 0));
	_currentBackBuffer = (_currentBackBuffer + 1) % _swapChainBufferCount;

	// Wait until frame commands are complete. This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	FlushCommandQueue();
}

void DemoApp::BuildRootSignature() {}

void DemoApp::BuildShaders() {}


void DemoApp::BuildInputLayout() {

}

void DemoApp::BuildGeometry() {

}

void DemoApp::BuildRenderItems() {

}

void DemoApp::BuildFrameResources() {

}

void DemoApp::BuildPSOs() {

}
