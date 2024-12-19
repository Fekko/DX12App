#pragma once

#if defined(DEBUG) || defined(_DEBUG)
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
#endif

#include "DxUtil.h"
#include "GameTimer.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")


class App
{
protected:

    App(HINSTANCE hInstance);
    App(const App& rhs) = delete;
    App& operator=(const App& rhs) = delete;
    virtual ~App();

public:

    static App* GetApp();

    HINSTANCE Instance() const;
    HWND MainWindow() const;
    float AspectRatio() const;

    int Run();

    virtual bool Initialize();
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
    virtual void CreateDescriptorHeaps();
    virtual void Update(const GameTimer& gt) = 0;
    virtual void Draw(const GameTimer& gt) = 0;

    virtual void OnResize();
    virtual void OnMouseDown(WPARAM, int, int) {}
    virtual void OnMouseUp(WPARAM, int, int) {}
    virtual void OnMouseMove(WPARAM, int, int) {}

protected:

    bool InitMainWindow();
    bool InitDirect3D();
    void CreateCommandObjects();
    void CreateSwapChain();

    void FlushCommandQueue();

    ID3D12Resource* CurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

    void CalculateFrameStats();

    void LogAdapters();

protected:

    static App* _pApp;

    HINSTANCE _instanceHandle{}; // application instance handle
    HWND _hWnd{}; // main window handle

    bool _paused{ }; // is the application paused?
    bool _minimized{ }; // is the application minimized?
    bool _maximized{ }; // is the application maximized?
    bool _resizing{ }; // are the resize bars being dragged?
    bool _fullScreenState{ }; // fullscreen enabled

    // MSAA = Multi Sampling Anti-aliasing
    // MSAA doesn't work like Frank Luna describes it
    const UINT MSAA_COUNT{ 1 };
    const UINT MSAA_QUALITY{ 0 };

    GameTimer _timer{};

    Microsoft::WRL::ComPtr<IDXGIFactory4> _pFactory{};
    Microsoft::WRL::ComPtr<IDXGISwapChain> _pSwapChain{};
    Microsoft::WRL::ComPtr<ID3D12Device> _pDevice{};

    Microsoft::WRL::ComPtr<ID3D12Fence> _pFence{};
    UINT64 _currentFence{};

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _pCommandQueue{};
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _pCommandAllocator{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _pCommandList{};

    static const int _swapChainBufferCount{ 2 };
    int _currentBackBuffer{};

    Microsoft::WRL::ComPtr<ID3D12Resource> _pSwapChainBuffer[_swapChainBufferCount]{};
    Microsoft::WRL::ComPtr<ID3D12Resource> _pDepthStencilBuffer{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _pRtvHeap{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _pDsvHeap{};

    UINT _rtvDescriptorSize{}; // rtv = Render Target View
    UINT _dsvDescriptorSize{}; // dsv = Depth Stencil View
    UINT _cbvSrvUavDescriptorSize{};

    D3D12_VIEWPORT _screenViewport{};
    D3D12_RECT _scissorRect{};

    // Derived class should set these in derived constructor to customize starting values.
    std::wstring _title{ L"Title" };
    D3D_DRIVER_TYPE _driverType{ D3D_DRIVER_TYPE_HARDWARE };
    DXGI_FORMAT _backBufferFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT _depthStencilFormat{ DXGI_FORMAT_D24_UNORM_S8_UINT };
    int _clientWidth{ 800 };
    int _clientHeight{ 600 };
};

