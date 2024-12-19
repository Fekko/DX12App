#include "App.h"

#include <WindowsX.h>
#include <cassert>
#include <vector>

#include "DxUtil.h"

using Microsoft::WRL::ComPtr;
using namespace DxUtil;

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return App::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

App* App::_pApp{};
App* App::GetApp()
{
    return _pApp;
}

App::App(HINSTANCE hInstance):	
	_instanceHandle( hInstance )
{
    assert(not _pApp);
    _pApp = this;
}

App::~App()
{
	if (_pDevice) {
		FlushCommandQueue();
	}
}

HINSTANCE App::Instance() const
{
	return _instanceHandle;
}

HWND App::MainWindow() const
{
	return _hWnd;
}

float App::AspectRatio() const
{
	return static_cast<float>( _clientWidth ) / _clientHeight;
}

int App::Run()
{
	MSG msg = {0};
 
	_timer.Reset();

	while(msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if(PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
		{
            TranslateMessage( &msg );
            DispatchMessage( &msg );
		}
		// Otherwise, do animation/game stuff.
		else
        {	
			_timer.Tick();

			if( not _paused )
			{
				CalculateFrameStats();
				Update(_timer);	
                Draw(_timer);
			}
			else
			{
				Sleep(100);
			}
        }
    }

	return (int)msg.wParam;
}

bool App::Initialize()
{
	if(not InitMainWindow()) return false;
	if(not InitDirect3D()) return false;
    OnResize();
	return true;
}
 
void App::CreateDescriptorHeaps()
{
	_rtvDescriptorSize = _pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	_dsvDescriptorSize = _pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	_cbvSrvUavDescriptorSize = _pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		.NumDescriptors = _swapChainBufferCount,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		.NodeMask = 0,
	};

    THROW_IF_FAILED(_pDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(_pRtvHeap.GetAddressOf())));


	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		.NumDescriptors = 1,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		.NodeMask = 0,
	};

    THROW_IF_FAILED(_pDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(_pDsvHeap.GetAddressOf())));
}

void App::OnResize()
{
#pragma region Reset
	assert(_pDevice);
	assert(_pSwapChain);
    assert(_pCommandAllocator);

	// Flush before changing any resources.
	FlushCommandQueue();

    THROW_IF_FAILED(_pCommandList->Reset(_pCommandAllocator.Get(), nullptr));

	// Release the previous resources we will be recreating.
	for (int i = 0; i < _swapChainBufferCount; ++i) {
		_pSwapChainBuffer[i].Reset();
	}
    _pDepthStencilBuffer.Reset();
#pragma endregion
	
	// Resize the swap chain.
    THROW_IF_FAILED(_pSwapChain->ResizeBuffers(
		_swapChainBufferCount, 
		_clientWidth, _clientHeight, 
		_backBufferFormat, 
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	_currentBackBuffer = 0;
 
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(_pRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < _swapChainBufferCount; i++)
	{
		THROW_IF_FAILED(_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&_pSwapChainBuffer[i])));
		_pDevice->CreateRenderTargetView(_pSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, _rtvDescriptorSize);
	}

	DXGI_SAMPLE_DESC sampleDesc {
		.Count = MSAA_COUNT,
		.Quality = MSAA_QUALITY,
	};

    // Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc{
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Alignment = 0,
		.Width = UINT(_clientWidth),
		.Height = UINT(_clientHeight),
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_R24G8_TYPELESS,
		.SampleDesc = sampleDesc,
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
	};

	D3D12_DEPTH_STENCIL_VALUE depthStencilValue {
		.Depth = 1.0f,
		.Stencil = 0,
	};

	D3D12_CLEAR_VALUE optClear{
		.Format = _depthStencilFormat,
		.DepthStencil = depthStencilValue,
	};

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    THROW_IF_FAILED(_pDevice->CreateCommittedResource(
        &heapProperties,
		D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(_pDepthStencilBuffer.GetAddressOf())));

	D3D12_TEX2D_DSV texture{ .MipSlice = 0, };
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{
		.Format = _depthStencilFormat,
		.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
		.Flags = D3D12_DSV_FLAG_NONE,
		.Texture2D = texture,
	};
    _pDevice->CreateDepthStencilView(_pDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    // Transition the resource from its initial state to be used as a depth buffer.
	auto resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		_pDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	_pCommandList->ResourceBarrier(1, &resourceBarrier);
    // Execute the resize commands.
    THROW_IF_FAILED(_pCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { _pCommandList.Get() };
    _pCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
	FlushCommandQueue();

	// Update the viewport transform to cover the client area.
	_screenViewport = D3D12_VIEWPORT{
		.TopLeftX = 0,
		.TopLeftY = 0,
		.Width = static_cast<float>(_clientWidth),
		.Height = static_cast<float>(_clientHeight),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f,
	};

    _scissorRect = D3D12_RECT {
		.left = 0, 
		.top = 0, 
		.right = _clientWidth, 
		.bottom = _clientHeight 
	};
}
 
LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch( msg )
	{
	// WM_ACTIVATE is sent when the window is activated or deactivated.  
	// We pause the game when the window is deactivated and unpause it 
	// when it becomes active.  
	case WM_ACTIVATE:
	{
		if( LOWORD(wParam) == WA_INACTIVE )
		{
			_paused = true;
			_timer.Stop();
		}
		else
		{
			_paused = false;
			_timer.Start();
		}
		return 0;
	}
	// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
	{
		// Save the new client area dimensions.
		_clientWidth  = LOWORD(lParam);
		_clientHeight = HIWORD(lParam);
		if( _pDevice )
		{
			if( wParam == SIZE_MINIMIZED )
			{
				_paused = true;
				_minimized = true;
				_maximized = false;
			}
			else if( wParam == SIZE_MAXIMIZED )
			{
				_paused = false;
				_minimized = false;
				_maximized = true;
				OnResize();
			}
			else if( wParam == SIZE_RESTORED )
			{
				
				// Restoring from minimized state?
				if( _minimized )
				{
					_paused = false;
					_minimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if( _maximized )
				{
					_paused = false;
					_maximized = false;
					OnResize();
				}
				else if( _resizing )
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;
	}
	// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
	{
		_paused = true;
		_resizing  = true;
		_timer.Stop();
		return 0;
	}
	// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
	// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
	{
		_paused = false;
		_resizing = false;
		_timer.Start();
		OnResize();
		return 0;
	}
	// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
	{
		PostQuitMessage(0);
		return 0;
	}
	// The WM_MENUCHAR message is sent when a menu is active and the user presses 
	// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
	{
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);
	}
	// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
	{
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200; 
		return 0;
	}
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	{
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	}
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
	{
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	}
	case WM_MOUSEMOVE:
	{
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	}
	case WM_KEYUP:
	{
		if (wParam == VK_ESCAPE) {
			PostQuitMessage(0);
		}
		else if ((int)wParam == VK_F2) {
			// DO stuff
		}
        return 0;
	}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool App::InitMainWindow()
{
	WNDCLASS wc{
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = MainWndProc,
		.cbClsExtra = 0,
		.cbWndExtra = 0,
		.hInstance = _instanceHandle,
		.hIcon = LoadIcon(0, IDI_APPLICATION),
		.hCursor = LoadCursor(0, IDC_ARROW),
		.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH),
		.lpszMenuName = 0,
		.lpszClassName = L"MainWnd",
	};

	if( !RegisterClass(&wc) )
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = RECT { 
		.left = 0, 
		.top = 0, 
		.right = _clientWidth,
		.bottom = _clientHeight 
	};

    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width  = R.right - R.left; // Is this needed?
	int height = R.bottom - R.top; // Is this needed?

	_hWnd = CreateWindow(
		L"MainWnd", 
		_title.c_str(), 
		WS_OVERLAPPEDWINDOW, 
		CW_USEDEFAULT, 
		CW_USEDEFAULT, 
		width, height, 
		0, 0, 
		_instanceHandle, 
		0); 

	if( not _hWnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(_hWnd, SW_SHOW);
	UpdateWindow(_hWnd);

	return true;
}

bool App::InitDirect3D()
{
	THROW_IF_FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_pFactory)));

#if defined(DEBUG) || defined(_DEBUG) 
	{
		// Enable the D3D12 debug layer.
		ComPtr<ID3D12Debug> debugController{};
		THROW_IF_FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
		LogAdapters();
	}
#endif

#pragma region 1) Create Device
	// Try to create hardware device.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr, // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&_pDevice));

	// Fallback to software rasterizer (WARP device)
	// WARP = Windows Advanced Rasterization Platform
	if(FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter{};
		THROW_IF_FAILED(_pFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		THROW_IF_FAILED(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&_pDevice)));
	}
#pragma endregion

#pragma region 2) Create Fence
	THROW_IF_FAILED(_pDevice->CreateFence(
		0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&_pFence)));
#pragma endregion

#pragma region 3) Create Command -Queue, -Allocator, -List
	CreateCommandObjects();
#pragma endregion

#pragma region 4) Create SwapChain
    CreateSwapChain();
#pragma endregion
	
#pragma region 5) Create DescriptorHeaps
	CreateDescriptorHeaps();
#pragma endregion

	return true;
}

void App::CreateCommandObjects()
{
#pragma region Create CommandQueue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
	};

	THROW_IF_FAILED(_pDevice->CreateCommandQueue(
		&queueDesc, 
		IID_PPV_ARGS(&_pCommandQueue))
	);
#pragma endregion

#pragma region Create CommandAllocator
	THROW_IF_FAILED(_pDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(_pCommandAllocator.GetAddressOf())
	));
#pragma endregion

#pragma region Create CommandList
	THROW_IF_FAILED(_pDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		_pCommandAllocator.Get(), // Associated command allocator
		nullptr, // Initial PipelineStateObject
		IID_PPV_ARGS(_pCommandList.GetAddressOf())
	));

	// Start off in a closed state.  This is because the first time we refer 
	// to the command list we will Reset it, and it needs to be closed before
	// calling Reset.
	_pCommandList->Close();
#pragma endregion
}

void App::CreateSwapChain()
{
    _pSwapChain.Reset();

	DXGI_RATIONAL refreshRate{
		.Numerator = 60,
		.Denominator = 1,
	};

	DXGI_MODE_DESC bufferDesc{
		.Width = UINT(_clientWidth),
		.Height = UINT(_clientHeight),
		.RefreshRate = refreshRate,
		.Format = _backBufferFormat,
		.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
		.Scaling = DXGI_MODE_SCALING_UNSPECIFIED,
	};

	DXGI_SAMPLE_DESC sampleDesc{
		.Count = MSAA_COUNT,
		.Quality = MSAA_QUALITY,
	};

	DXGI_SWAP_CHAIN_DESC swapChainDesc{
		.BufferDesc = bufferDesc,
		.SampleDesc = sampleDesc,
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = _swapChainBufferCount,
		.OutputWindow = _hWnd,
		.Windowed = true,
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
	};

    THROW_IF_FAILED(_pFactory->CreateSwapChain(
		_pCommandQueue.Get(),
		&swapChainDesc,
		_pSwapChain.GetAddressOf()
	));
}

void App::FlushCommandQueue()
{
	// Advance the fence value to mark commands up to this fence point.
	_currentFence++;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	THROW_IF_FAILED(_pCommandQueue->Signal(
		_pFence.Get(),
		_currentFence
	));

	// Wait until the GPU has completed commands up to this fence point.
	if (_pFence->GetCompletedValue() < _currentFence) {
		HANDLE eventHandle = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		THROW_IF_FAILED(_pFence->SetEventOnCompletion(
			_currentFence,
			eventHandle
		));

		// Wait until the GPU hits current fence event is fired.
		assert(eventHandle != 0);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* App::CurrentBackBuffer() const
{
	return _pSwapChainBuffer[_currentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE App::CurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		_pRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		_currentBackBuffer,
		_rtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE App::DepthStencilView() const
{
	return _pDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

// TODO: CLEANUP
void App::CalculateFrameStats()
{
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;

	// Compute averages over one second period.
	if( (_timer.TotalTime() - timeElapsed) >= 1.0f )
	{ 
		// fps = frameCnt / 1
		float fps = (float)frameCnt;
		float mspf = 1000.0f / fps;

        std::wstring fpsStr = std::to_wstring(fps);
		std::wstring mspfStr = std::to_wstring(mspf);

		std::wstring windowText = _title +
            L"    fps: " + fpsStr +
            L"   mspf: " + mspfStr;

        SetWindowText(_hWnd, windowText.c_str());
		
		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

void App::LogAdapters()
{
    UINT i = 0;
	IDXGIAdapter* pAdapter{};
	std::vector<IDXGIAdapter*> pAdapters{};
    while(_pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
		DXGI_ADAPTER_DESC desc{};
		pAdapter->GetDesc(&desc);

        std::wstring text = L"***Adapter: ";
        text += desc.Description;
        text += L"\n";

        OutputDebugString(text.c_str());

		pAdapters.push_back(pAdapter);
        
        ++i;
    }

    for(size_t j = 0; j < pAdapters.size(); ++j)
    {
        LogAdapterOutputs(pAdapters[j]);
        RELEASE_COM(pAdapters[j]);
    }
}