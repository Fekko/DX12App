#include "WavesApp.h"

#include <array>
#include <DirectXColors.h>
#include <d3dcompiler.h>

#include "GeometryGenerator.h"
#include "MeshGeometry.h"

using namespace DirectX;
using namespace Microsoft::WRL;

WavesApp::WavesApp(HINSTANCE hInstance)
	: App(hInstance) 
{
	_title = L"WavesApp";  
}

WavesApp::~WavesApp() {
	if (_pDevice) FlushCommandQueue();
} 

bool WavesApp::Initialize() {
	if (not App::Initialize()) return false;

	// Reset the command list to prep for initialization commands.
	THROW_IF_FAILED(_pCommandList->Reset(_pCommandAllocator.Get(), nullptr));


	_pWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildLandGeometry();
	BuildWavesGeometryBuffers();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands
	THROW_IF_FAILED(_pCommandList->Close());
	ID3D12CommandList* pCommandLists[] = { _pCommandList.Get() };
	_pCommandQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

	// Wait until initialization is complete
	FlushCommandQueue();

	return true;
}

void WavesApp::OnResize() {
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

void WavesApp::Update(const GameTimer& gt) {
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	_currentFrameResourceIndex = (_currentFrameResourceIndex + 1) % RenderItem::NrFrameResources;
	_pCurrentFrameResource = _frameResources[_currentFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (_pCurrentFrameResource->Fence != 0 and _pFence->GetCompletedValue() < _pCurrentFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
		THROW_IF_FAILED(_pFence->SetEventOnCompletion(_pCurrentFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void WavesApp::Draw(const GameTimer& /*timer*/) {
	auto pCommandListAllocator = _pCurrentFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	THROW_IF_FAILED(pCommandListAllocator->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (_isWireframe) {
		THROW_IF_FAILED(_pCommandList->Reset(pCommandListAllocator.Get(), _pipelineStateObjects["opaque_wireframe"].Get()));
	}
	else {
		THROW_IF_FAILED(_pCommandList->Reset(pCommandListAllocator.Get(), _pipelineStateObjects["opaque"].Get()));
	}

	// Set the viewport and scissor rect. This needs to be reset whenever the command list is reset.
	_pCommandList->RSSetViewports(1, &_screenViewport);
	_pCommandList->RSSetScissorRects(1, &_scissorRect);

	// Indicate a state transition on the resource usage.
	auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_pCommandList->ResourceBarrier(1, &barrier1);

	// Clear the back buffer and depth buffer.
	_pCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	_pCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	auto currentBackBufferView = CurrentBackBufferView();
	auto depthStencilView = DepthStencilView();
	// OM = Output Merger stage
	_pCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

	_pCommandList->SetGraphicsRootSignature(_pRootSignature.Get());

	// Bind per-pass constant buffer.  We only need to do this once per-pass.
	auto passCB = _pCurrentFrameResource->PassCBuffer->Resource();
	_pCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	DrawRenderItems(_pCommandList.Get(), _opaqueRenderItems);

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

	// Advance the fence value to mark commands up to this fence point.
	_pCurrentFrameResource->Fence = ++_currentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	_pCommandQueue->Signal(_pFence.Get(), _currentFence);
}

void WavesApp::OnMouseDown(WPARAM /*btnState*/, int x, int y) {
	_lastMousePosition.x = x;
	_lastMousePosition.y = y;

	SetCapture(_hWnd);
}

void WavesApp::OnMouseUp(WPARAM /*btnState*/, int /*x*/, int /*y*/) {
	ReleaseCapture();
}

void WavesApp::OnMouseMove(WPARAM btnState, int x, int y) {
	if ((btnState & MK_LBUTTON)) {
		// Each pixel corresponds with a quarter of a degree
		float dx = XMConvertToRadians(.25f * static_cast<float>(x - _lastMousePosition.x));
		float dy = XMConvertToRadians(.25f * static_cast<float>(y - _lastMousePosition.y));

		_theta += dx;
		_phi += dy;

		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON)) {
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - _lastMousePosition.x);
		float dy = 0.05f * static_cast<float>(y - _lastMousePosition.y);

		// Update the camera radius based on input.
		_radius += dx - dy;

		// Restrict the radius.
		_radius = MathHelper::Clamp(_radius, 5.0f, 150.0f);
	}

	_lastMousePosition.x = x;
	_lastMousePosition.y = y;
}

void WavesApp::OnKeyboardInput(const GameTimer& gt) {
	// Check if most significant bit is set
	if (GetAsyncKeyState(VK_TAB) & 0x8000)
		_isWireframe = true;
	else
		_isWireframe = false;
}

void WavesApp::UpdateCamera(const GameTimer& gt) {
	// Convert Spherical to Cartesian coordinates.
	_eyePos.x = _radius * sinf(_phi) * cosf(_theta);
	_eyePos.z = _radius * sinf(_phi) * sinf(_theta);
	_eyePos.y = _radius * cosf(_phi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(_eyePos.x, _eyePos.y, _eyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&_view, view);
}

void WavesApp::UpdateObjectCBs(const GameTimer& gt) {
	auto currObjectCB = _pCurrentFrameResource->ObjectCBuffer.get();
	for (auto& pItem : _renderItems) {
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (pItem->NrFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&pItem->World);

			ObjectConstants objConstants{};
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(pItem->ObjectCBufferIndex, objConstants);

			pItem->NrFramesDirty--;
		}
	}
}

void WavesApp::UpdateMainPassCB(const GameTimer& gt) {
	XMMATRIX view = XMLoadFloat4x4(&_view);
	XMMATRIX proj = XMLoadFloat4x4(&_projection);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	auto viewDeterminant = XMMatrixDeterminant(view);
	XMMATRIX invView = XMMatrixInverse(&viewDeterminant, view);
	auto projDeterminant = XMMatrixDeterminant(proj);
	XMMATRIX invProj = XMMatrixInverse(&projDeterminant, proj);
	auto viewProjDeterminant = XMMatrixDeterminant(viewProj);
	XMMATRIX invViewProj = XMMatrixInverse(&viewProjDeterminant, viewProj);

	XMStoreFloat4x4(&_mainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&_mainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&_mainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&_mainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&_mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&_mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	_mainPassCB.EyePosition = _eyePos;
	_mainPassCB.RenderTargetSize = XMFLOAT2((float)_clientWidth, (float)_clientHeight);
	_mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / _clientWidth, 1.0f / _clientHeight);
	_mainPassCB.NearZ = 1.0f;
	_mainPassCB.FarZ = 1000.0f;
	_mainPassCB.TotalTime = gt.TotalTime();
	_mainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = _pCurrentFrameResource->PassCBuffer.get();
	currPassCB->CopyData(0, _mainPassCB);
}

void WavesApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((_timer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, _pWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, _pWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		_pWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	_pWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = _pCurrentFrameResource->WavesVertexBuffer.get();
	for (int i = 0; i < _pWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = _pWaves->Position(i);
		v.Color = XMFLOAT4(DirectX::Colors::Blue);

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	_pWavesRenderItem->pMeshGeometry->VertexBufferGpu = currWavesVB->Resource();
}

void WavesApp::BuildRootSignature() {
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBV.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig{};
	ComPtr<ID3DBlob> errorBlob{};
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	THROW_IF_FAILED(hr);

	THROW_IF_FAILED(_pDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(_pRootSignature.GetAddressOf())));
}

void WavesApp::BuildShaders() {
	_shaders["standardVS"] = DxUtil::LoadBinary(L"color.vs.cso");
	_shaders["opaquePS"] = DxUtil::LoadBinary(L"color.ps.cso");
}

void WavesApp::BuildInputLayout() {
	D3D12_INPUT_ELEMENT_DESC positionDesc{
		.SemanticName = "Position",
		.SemanticIndex = 0,
		.Format = DXGI_FORMAT_R32G32B32_FLOAT,
		.InputSlot = 0,
		.AlignedByteOffset = 0,
		.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
		.InstanceDataStepRate = 0,
	};

	D3D12_INPUT_ELEMENT_DESC elementDesc{
	.SemanticName = "Color",
	.SemanticIndex = 0,
	.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
	.InputSlot = 0,
	.AlignedByteOffset = 12,
	.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
	.InstanceDataStepRate = 0,
	};

	_inputLayout = { positionDesc , elementDesc };
}

void WavesApp::BuildLandGeometry()
{
	GeometryGenerator geometryGenerator{};
	GeometryGenerator::MeshData grid = geometryGenerator.CreateGrid(160.0f, 160.0f, 50, 50);

	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);

		// Color the vertex based on its height.
		if (vertices[i].Pos.y < -10.0f)
		{
			// Sandy beach color.
			vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
		}
		else if (vertices[i].Pos.y < 5.0f)
		{
			// Light yellow-green.
			vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
		}
		else if (vertices[i].Pos.y < 12.0f)
		{
			// Dark yellow-green.
			vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
		}
		else if (vertices[i].Pos.y < 20.0f)
		{
			// Dark brown.
			vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
		}
		else
		{
			// White snow.
			vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	const UINT vertexBufferByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT indexBufferByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->Name = "landGeo";

	THROW_IF_FAILED(D3DCreateBlob(vertexBufferByteSize, &geometry->VertexBufferCpu));
	CopyMemory(geometry->VertexBufferCpu->GetBufferPointer(), vertices.data(), vertexBufferByteSize);

	THROW_IF_FAILED(D3DCreateBlob(indexBufferByteSize, &geometry->IndexBufferCpu));
	CopyMemory(geometry->IndexBufferCpu->GetBufferPointer(), indices.data(), indexBufferByteSize);

	geometry->VertexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);

	geometry->IndexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);

	geometry->VertexByteStride = sizeof(Vertex);
	geometry->VertexBufferByteSize = vertexBufferByteSize;
	geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->IndexBufferByteSize = indexBufferByteSize;

	SubMeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geometry->DrawArguments["grid"] = submesh;

	_geometries["landGeo"] = std::move(geometry);
}

void WavesApp::BuildWavesGeometryBuffers()
{
	std::vector<std::uint16_t> indices(3 * _pWaves->TriangleCount()); // 3 indices per face
	assert(_pWaves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = _pWaves->RowCount();
	int n = _pWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vertexBufferByteSize = _pWaves->VertexCount() * sizeof(Vertex);
	UINT indexBufferByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->Name = "waterGeo";

	// Set dynamically.
	geometry->VertexBufferCpu = nullptr;
	geometry->VertexBufferGpu = nullptr;

	THROW_IF_FAILED(D3DCreateBlob(indexBufferByteSize, &geometry->IndexBufferCpu));
	CopyMemory(geometry->IndexBufferCpu->GetBufferPointer(), indices.data(), indexBufferByteSize);

	geometry->IndexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);

	geometry->VertexByteStride = sizeof(Vertex);
	geometry->VertexBufferByteSize = vertexBufferByteSize;
	geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->IndexBufferByteSize = indexBufferByteSize;

	SubMeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geometry->DrawArguments["grid"] = submesh;

	_geometries["waterGeo"] = std::move(geometry);
}

void WavesApp::BuildPSOs() {
#pragma region Opaque
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc
	{
		.pInputElementDescs = _inputLayout.data(),
		.NumElements = (UINT)_inputLayout.size(),
	};

	D3D12_SHADER_BYTECODE vs
	{
		.pShaderBytecode = _shaders["standardVS"]->GetBufferPointer(),
		.BytecodeLength = _shaders["standardVS"]->GetBufferSize(),
	};

	D3D12_SHADER_BYTECODE ps
	{
		.pShaderBytecode = _shaders["opaquePS"]->GetBufferPointer(),
		.BytecodeLength = _shaders["opaquePS"]->GetBufferSize(),
	};

	DXGI_SAMPLE_DESC sampleDesc
	{
		.Count = 1,
		.Quality = 0,
	};

	CD3DX12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc{
		.pRootSignature = _pRootSignature.Get(),
		.VS = vs,
		.PS = ps,
		.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
		.SampleMask = UINT_MAX,
		.RasterizerState = rasterizerDesc,
		.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
		.InputLayout = inputLayoutDesc,
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.DSVFormat = _depthStencilFormat,
		.SampleDesc = sampleDesc,
	};
	opaquePsoDesc.RTVFormats[0] = _backBufferFormat;

	THROW_IF_FAILED(_pDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&_pipelineStateObjects["opaque"])));
#pragma endregion Opaque

#pragma region Opaque Wireframe
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	THROW_IF_FAILED(_pDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&_pipelineStateObjects["opaque_wireframe"])));
#pragma endregion Opaque Wireframe
}

void WavesApp::BuildFrameResources() {
	for (int i = 0; i < RenderItem::NrFrameResources; ++i) {
		_frameResources.push_back(std::make_unique<FrameResource>(_pDevice.Get(),
			1, (UINT)_renderItems.size(), _pWaves->VertexCount()));
	}
}

void WavesApp::BuildRenderItems()
{
	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	wavesRitem->ObjectCBufferIndex = 0;
	wavesRitem->pMeshGeometry = _geometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->pMeshGeometry->DrawArguments["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->pMeshGeometry->DrawArguments["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->pMeshGeometry->DrawArguments["grid"].BaseVertexLocation;

	_pWavesRenderItem = wavesRitem.get();
	_opaqueRenderItems.push_back(wavesRitem.get());

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjectCBufferIndex = 1;
	gridRitem->pMeshGeometry = _geometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->pMeshGeometry->DrawArguments["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->pMeshGeometry->DrawArguments["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->pMeshGeometry->DrawArguments["grid"].BaseVertexLocation;

	_opaqueRenderItems.push_back(gridRitem.get());

	_renderItems.push_back(std::move(wavesRitem));
	_renderItems.push_back(std::move(gridRitem));
}

void WavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = _pCurrentFrameResource->ObjectCBuffer->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		auto vertexBufferView = ri->pMeshGeometry->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
		auto indexBufferView = ri->pMeshGeometry->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBufferView);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
		objCBAddress += ri->ObjectCBufferIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

float WavesApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 WavesApp::GetHillsNormal(float x, float z) const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}