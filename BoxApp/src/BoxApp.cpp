#include "BoxApp.h"

#include <array>
#include <DirectXColors.h>
#include <d3dcompiler.h>

using namespace DirectX;

BoxApp::BoxApp(HINSTANCE hInstance)
	: App(hInstance) 
{
	_title = L"BoxApp";
}

BoxApp::~BoxApp() {
	if (_pDevice) FlushCommandQueue();
}

bool BoxApp::Initialize() {
	if (not App::Initialize()) return false;

	// Reset the command list to prep for initialization commands.
	THROW_IF_FAILED(_pCommandList->Reset(_pCommandAllocator.Get(), nullptr));

	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildBoxGeometry();
	BuildPipelineStateObject();

	// Execute the initialization commands
	THROW_IF_FAILED(_pCommandList->Close());
	ID3D12CommandList* pCommandLists[] = { _pCommandList.Get() };
	_pCommandQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

	// Wait until initialization is complete
	FlushCommandQueue();

	return true;
}

void BoxApp::OnResize() {
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

void BoxApp::Update(const GameTimer&) {
	// Convert Spherical to Carthesian coordinates
	float x = _radius * sinf(_phi) * cosf(_theta);
	float y = _radius * cosf(_phi);
	float z = _radius * sinf(_phi) * sinf(_theta);

	// Build the view matrix
	XMVECTOR eye = XMVectorSet(x, y, z, 1);
	XMVECTOR focus = XMVectorSet(0, 0, 0, 1);
	XMVECTOR up = XMVectorSet(0, 1, 0, 0);

	XMMATRIX view = XMMatrixLookAtLH(eye, focus, up);
	XMStoreFloat4x4(&_view, view);

	XMMATRIX world = XMLoadFloat4x4(&_world);
	XMMATRIX projection = XMLoadFloat4x4(&_projection);
	XMMATRIX worldViewProjection = world * view * projection;

	// Update the constant buffers with the latest wvp matrix
	ObjectConstants constants;
	XMStoreFloat4x4(&constants.WorldViewProj, XMMatrixTranspose(worldViewProjection));
	_pUploadBuffer->CopyData(0,constants);
}

void BoxApp::Draw(const GameTimer& /*timer*/) {
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	THROW_IF_FAILED(_pCommandAllocator->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	THROW_IF_FAILED(_pCommandList->Reset(_pCommandAllocator.Get(), _pPipelineStateObject.Get()));

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

	auto iBufferView = _pMeshGeometry->IndexBufferView();
	auto vBufferView = _pMeshGeometry->VertexBufferView();
	_pCommandList->IASetIndexBuffer(&iBufferView);
	_pCommandList->IASetVertexBuffers(0, 1,&vBufferView);

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

void BoxApp::OnMouseDown(WPARAM /*btnState*/, int x, int y) 
{
	_lastMousePosition.x = x;
	_lastMousePosition.y = y;

	SetCapture(_hWnd);
}

void BoxApp::OnMouseUp(WPARAM /*btnState*/, int /*x*/, int /*y*/)
{
	ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON)) {
		// Each pixel corresponds with a quarter of a degree
		float dx = XMConvertToRadians(.25f * static_cast<float>(x - _lastMousePosition.x));
		float dy = XMConvertToRadians(.25f * static_cast<float>(y - _lastMousePosition.y));

		_theta += dx;
		_phi += dy;

		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON)) {
		float dx = XMConvertToRadians(.25f * static_cast<float>(x - _lastMousePosition.x));
		float dy = XMConvertToRadians(.25f * static_cast<float>(y - _lastMousePosition.y));

		_radius += dx - dy;
		_radius = MathHelper::Clamp(_radius, 3.0f, 15.f);
	}

	_lastMousePosition.x = x;
	_lastMousePosition.y = y;
}

void BoxApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeap{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = 1,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		.NodeMask = 0,
	};

	// IID = Interface Identifier
	// PPV = **void
	THROW_IF_FAILED(_pDevice->CreateDescriptorHeap(&cbvHeap, IID_PPV_ARGS(&_pCbvHeap)));
}

void BoxApp::BuildConstantBuffers()
{
	_pUploadBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(_pDevice.Get(), 1, true);

	UINT byteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	D3D12_GPU_VIRTUAL_ADDRESS address = _pUploadBuffer->Resource()->GetGPUVirtualAddress();
	int index = 0;
	address += index * byteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC desc
	{
		.BufferLocation = address,
		.SizeInBytes = byteSize,
	};

	_pDevice->CreateConstantBufferView(&desc, _pCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
	// See the shader program as a function, and this as it's signature

	// Root parameter can be: table, desciptor, contstants
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];

	// Create single descriptor table of CBVs
	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	// A root signature is an array of root parameters
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// Create a root signature with a single slot which points to 
	// a descriptor range consisting of a single constant buffer
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig{};
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob{};

	HRESULT result = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (errorBlob) 
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	THROW_IF_FAILED(result);

	THROW_IF_FAILED(_pDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_pRootSignature)));
}

void BoxApp::BuildShaders()
{
	_vertexShader = DxUtil::LoadBinary(L"color.vs.cso");
	_pixelShader = DxUtil::LoadBinary(L"color.ps.cso");
}


void BoxApp::BuildInputLayout()
{
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

void BoxApp::BuildBoxGeometry()
{
	_pMeshGeometry = std::make_unique<MeshGeometry>();
	_pMeshGeometry->Name = "box-geometry";

	std::array<Vertex, 8> vertices
	{
		XMFLOAT3(-1, -1, -1), XMFLOAT4(Colors::White),
		XMFLOAT3(-1,  1, -1), XMFLOAT4(Colors::Black),
		XMFLOAT3(1,  1, -1), XMFLOAT4(Colors::Red),
		XMFLOAT3(1, -1, -1), XMFLOAT4(Colors::Green),
		XMFLOAT3(-1, -1,  1), XMFLOAT4(Colors::Blue),
		XMFLOAT3(-1,  1,  1), XMFLOAT4(Colors::Yellow),
		XMFLOAT3(1,  1,  1), XMFLOAT4(Colors::Cyan),
		XMFLOAT3(1, -1,  1), XMFLOAT4(Colors::Magenta),
	};

	std::array<uint16_t, 36> indices
	{
		// Front
		0, 1, 2,
		0, 2, 3,
		// Back
		4, 6, 5,
		4, 7, 6,
		// Left
		4, 5, 1,
		4, 1, 0,
		// Right
		3, 2, 6,
		3, 6, 7,
		// Top
		1, 5, 6,
		1, 6, 2,
		// Bottom
		4, 0, 3,
		4, 3, 7,
	};

	// Vertex buffer
	const UINT vBufferByteSize = (UINT) (vertices.size() * sizeof(Vertex));
	THROW_IF_FAILED(D3DCreateBlob(vBufferByteSize, _pMeshGeometry->VertexBufferCpu.GetAddressOf()));
	DxUtil::CreateDefaultBuffer(_pDevice.Get(), _pCommandList.Get(), 
		_pMeshGeometry->VertexBufferCpu.GetAddressOf(), vBufferByteSize, _pMeshGeometry->VertexBufferUploader);

	_pMeshGeometry->VertexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), vertices.data(), vBufferByteSize, _pMeshGeometry->VertexBufferUploader);

	// index buffer
	const UINT iBufferByteSize = (UINT)(indices.size() * sizeof(uint16_t));
	THROW_IF_FAILED(D3DCreateBlob(iBufferByteSize, _pMeshGeometry->IndexBufferCpu.GetAddressOf()));
	DxUtil::CreateDefaultBuffer(_pDevice.Get(), _pCommandList.Get(),
		_pMeshGeometry->IndexBufferCpu.GetAddressOf(), iBufferByteSize, _pMeshGeometry->IndexBufferUploader);

	_pMeshGeometry->IndexBufferGpu = DxUtil::CreateDefaultBuffer(_pDevice.Get(),
		_pCommandList.Get(), indices.data(), iBufferByteSize, _pMeshGeometry->IndexBufferUploader);


	_pMeshGeometry->VertexByteStride = sizeof(Vertex);
	_pMeshGeometry->VertexBufferByteSize = vBufferByteSize;
	_pMeshGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	_pMeshGeometry->IndexBufferByteSize = iBufferByteSize;

	_pMeshGeometry->DrawArguments["box"] = {
		.IndexCount = (UINT) indices.size(),
		.StartIndexLocation = 0,
		.BaseVertexLocation = 0,
	};
}

void BoxApp::BuildPipelineStateObject()
{
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc
	{
		.pInputElementDescs = _inputLayout.data(),
		.NumElements = (UINT)_inputLayout.size(),
	};

	D3D12_SHADER_BYTECODE vs
	{
		.pShaderBytecode = _vertexShader->GetBufferPointer(),
		.BytecodeLength = _vertexShader->GetBufferSize(),
	};

	D3D12_SHADER_BYTECODE ps
	{
		.pShaderBytecode = _pixelShader->GetBufferPointer(),
		.BytecodeLength = _pixelShader->GetBufferSize(),
	};

	DXGI_SAMPLE_DESC sampleDesc
	{
		.Count = 1,
		.Quality = 0,
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc
	{
		.pRootSignature = _pRootSignature.Get(),
		.VS = vs,
		.PS = ps,
		.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
		.SampleMask = UINT_MAX,
		.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
		.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
		.InputLayout = inputLayoutDesc,
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.DSVFormat = _depthStencilFormat,
		.SampleDesc = sampleDesc,
	};

	psoDesc.RTVFormats[0] = _backBufferFormat;

	THROW_IF_FAILED(_pDevice->CreateGraphicsPipelineState(
		&psoDesc, 
		IID_PPV_ARGS(&_pPipelineStateObject)
	));
}