#include "ShapesApp.h"

#include <array>
#include <DirectXColors.h>
#include <d3dcompiler.h>

#include "GeometryGenerator.h"
#include "MeshGeometry.h"

using namespace DirectX;
using namespace Microsoft::WRL;

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: App(hInstance) 
{
	_title = L"ShapesApp";  
}

ShapesApp::~ShapesApp() {
	if (_pDevice) FlushCommandQueue();
} 

bool ShapesApp::Initialize() {
	if (not App::Initialize()) return false;

	// Reset the command list to prep for initialization commands.
	THROW_IF_FAILED(_pCommandList->Reset(_pCommandAllocator.Get(), nullptr));

	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands
	THROW_IF_FAILED(_pCommandList->Close());
	ID3D12CommandList* pCommandLists[] = { _pCommandList.Get() };
	_pCommandQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

	// Wait until initialization is complete
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize() {
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

void ShapesApp::Update(const GameTimer& gt) {
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
}

void ShapesApp::Draw(const GameTimer& /*timer*/) {
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

	ID3D12DescriptorHeap* descriptorHeaps[] = { _pCbvHeap.Get() };
	_pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	_pCommandList->SetGraphicsRootSignature(_pRootSignature.Get());

	int passCbvIndex = _passCbvOffset + _currentFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(_pCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, _cbvSrvUavDescriptorSize);
	_pCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

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

void ShapesApp::OnMouseDown(WPARAM /*btnState*/, int x, int y) {
	_lastMousePosition.x = x;
	_lastMousePosition.y = y;

	SetCapture(_hWnd);
}

void ShapesApp::OnMouseUp(WPARAM /*btnState*/, int /*x*/, int /*y*/) {
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y) {
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

void ShapesApp::OnKeyboardInput(const GameTimer& gt) {
	// Check if most significant bit is set
	if (GetAsyncKeyState(VK_TAB) & 0x8000)
		_isWireframe = true;
	else
		_isWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt) {
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

void ShapesApp::UpdateObjectCBs(const GameTimer& gt) {
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

void ShapesApp::UpdateMainPassCB(const GameTimer& gt) {
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

void ShapesApp::BuildDescriptorHeaps() {
	UINT objCount = (UINT)_opaqueRenderItems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * RenderItem::NrFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	_passCbvOffset = objCount * RenderItem::NrFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = numDescriptors,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		.NodeMask = 0,
	};

	// IID = Interface Identifier
	// PPV = **void
	THROW_IF_FAILED(_pDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&_pCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews() {
	UINT ObjectConstantsByteSize = MathHelper::ByteSize(sizeof(ObjectConstants));
	UINT objCount = (UINT)_opaqueRenderItems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < RenderItem::NrFrameResources; ++frameIndex) {
		auto pObjectCB = _frameResources[frameIndex]->ObjectCBuffer->Resource();
		for (UINT i = 0; i < objCount; ++i) {
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = pObjectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * ObjectConstantsByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(_pCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, _cbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{
				.BufferLocation = cbAddress,
				.SizeInBytes = ObjectConstantsByteSize,
			};

			_pDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passConstantsByteSize = MathHelper::ByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < RenderItem::NrFrameResources; ++frameIndex) {
		auto passCB = _frameResources[frameIndex]->PassCBuffer->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = _passCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(_pCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, _cbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{
			.BufferLocation = cbAddress,
			.SizeInBytes = passConstantsByteSize,
		};

		_pDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature() {
	CD3DX12_DESCRIPTOR_RANGE cbvTable0{};
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1{};
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2]{};

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSignature{};
	ComPtr<ID3DBlob> errorBlob{};
	HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSignature.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	THROW_IF_FAILED(hr);

	THROW_IF_FAILED(_pDevice->CreateRootSignature(
		0,
		serializedRootSignature->GetBufferPointer(),
		serializedRootSignature->GetBufferSize(),
		IID_PPV_ARGS(_pRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShaders() {
	_shaders["standardVS"] = DxUtil::LoadBinary(L"color.vs.cso");
	_shaders["opaquePS"] = DxUtil::LoadBinary(L"color.ps.cso");
}

void ShapesApp::BuildInputLayout() {
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

void ShapesApp::BuildShapeGeometry() {

	GeometryGenerator geometryGenerator{};
	GeometryGenerator::MeshData box = geometryGenerator.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geometryGenerator.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geometryGenerator.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geometryGenerator.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	// Define the SubmeshGeometry that cover different 
	// regions of the vertex/index buffers.

	SubMeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubMeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubMeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubMeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto pGeometry = std::make_unique<MeshGeometry>();
	pGeometry->Name = "shapeGeo";

	THROW_IF_FAILED(D3DCreateBlob(vbByteSize, &pGeometry->VertexBufferCpu));
	CopyMemory(pGeometry->VertexBufferCpu->GetBufferPointer(), vertices.data(), vbByteSize);

	THROW_IF_FAILED(D3DCreateBlob(ibByteSize, &pGeometry->IndexBufferCpu));
	CopyMemory(pGeometry->IndexBufferCpu->GetBufferPointer(), indices.data(), ibByteSize);

	pGeometry->VertexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), vertices.data(), vbByteSize, pGeometry->VertexBufferUploader);

	pGeometry->IndexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), indices.data(), ibByteSize, pGeometry->IndexBufferUploader);

	pGeometry->VertexByteStride = sizeof(Vertex);
	pGeometry->VertexBufferByteSize = vbByteSize;
	pGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	pGeometry->IndexBufferByteSize = ibByteSize;

	pGeometry->DrawArguments["box"] = boxSubmesh;
	pGeometry->DrawArguments["grid"] = gridSubmesh;
	pGeometry->DrawArguments["sphere"] = sphereSubmesh;
	pGeometry->DrawArguments["cylinder"] = cylinderSubmesh;

	_geometries[pGeometry->Name] = std::move(pGeometry);
}

void ShapesApp::BuildPSOs() {
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

void ShapesApp::BuildFrameResources() {
	for (int i = 0; i < RenderItem::NrFrameResources; ++i) {
		_frameResources.push_back(std::make_unique<FrameResource>(
			_pDevice.Get(),
			1, 
			(UINT)_renderItems.size()));
	}
}

void ShapesApp::BuildRenderItems() {
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	boxRitem->ObjectCBufferIndex = 0;
	boxRitem->pMeshGeometry = _geometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->pMeshGeometry->DrawArguments["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->pMeshGeometry->DrawArguments["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->pMeshGeometry->DrawArguments["box"].BaseVertexLocation;
	_renderItems.push_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjectCBufferIndex = 1;
	gridRitem->pMeshGeometry = _geometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->pMeshGeometry->DrawArguments["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->pMeshGeometry->DrawArguments["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->pMeshGeometry->DrawArguments["grid"].BaseVertexLocation;
	_renderItems.push_back(std::move(gridRitem));

	UINT objCBIndex = 2;
	for (int i = 0; i < 5; ++i) {
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		leftCylRitem->ObjectCBufferIndex = objCBIndex++;
		leftCylRitem->pMeshGeometry = _geometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->pMeshGeometry->DrawArguments["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->pMeshGeometry->DrawArguments["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->pMeshGeometry->DrawArguments["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		rightCylRitem->ObjectCBufferIndex = objCBIndex++;
		rightCylRitem->pMeshGeometry = _geometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->pMeshGeometry->DrawArguments["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->pMeshGeometry->DrawArguments["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->pMeshGeometry->DrawArguments["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjectCBufferIndex = objCBIndex++;
		leftSphereRitem->pMeshGeometry = _geometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->pMeshGeometry->DrawArguments["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->pMeshGeometry->DrawArguments["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->pMeshGeometry->DrawArguments["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjectCBufferIndex = objCBIndex++;
		rightSphereRitem->pMeshGeometry = _geometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->pMeshGeometry->DrawArguments["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->pMeshGeometry->DrawArguments["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->pMeshGeometry->DrawArguments["sphere"].BaseVertexLocation;

		_renderItems.push_back(std::move(leftCylRitem));
		_renderItems.push_back(std::move(rightCylRitem));
		_renderItems.push_back(std::move(leftSphereRitem));
		_renderItems.push_back(std::move(rightSphereRitem));
	}

	// All the render items are opaque.
	for (auto& e : _renderItems)
		_opaqueRenderItems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* pCommandList, const std::vector<RenderItem*>& items) {
	UINT objCBByteSize = MathHelper::ByteSize(sizeof(ObjectConstants));

	// For each render item...
	for (size_t i = 0; i < items.size(); ++i) {
		auto ri = items[i];

		auto vBufferView = ri->pMeshGeometry->VertexBufferView();
		pCommandList->IASetVertexBuffers(0, 1, &vBufferView);
		auto iBufferView = ri->pMeshGeometry->IndexBufferView();
		pCommandList->IASetIndexBuffer(&iBufferView);
		pCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.
		UINT cbvIndex = _currentFrameResourceIndex * (UINT)_opaqueRenderItems.size() + ri->ObjectCBufferIndex;
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(_pCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, _cbvSrvUavDescriptorSize);

		pCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		pCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}