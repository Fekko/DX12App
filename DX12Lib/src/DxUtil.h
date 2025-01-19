#pragma once

#include <string>
#include <Windows.h>
#include <wrl.h>
#include <dxgi1_6.h> // DirectX Shared
#include <d3d12.h>   // DirectX 3D specific 
#include <directxcollision.h>

#include "d3dx12.h"  // Microsoft helper functions

namespace DxUtil
{
    UINT CalcConstantBufferByteSize(UINT byteSize);

    void LogAdapterOutputs(
        IDXGIAdapter* adapter
    );

    void LogOutputDisplayModes(
        IDXGIOutput* output, 
        DXGI_FORMAT format
    );

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
        ID3D12Device* pDevice, 
        ID3D12GraphicsCommandList* pCommandList,
        const void* pInitData,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer
    );

    class FileNotFoundException : std::exception
    {
    public:
        FileNotFoundException(const std::wstring& filename);
        const std::wstring Filename{};
    };

    class DxException
    {
    public:
        DxException() = default;
        DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

        std::wstring ToString()const;

        HRESULT ErrorCode = S_OK;
        std::wstring FunctionName;
        std::wstring Filename;
        int LineNumber = -1;
    };

    inline std::wstring AnsiToWString(const std::string& str) {
        WCHAR buffer[512];
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
        return std::wstring(buffer);
    }

    Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);
}

#define THROW_IF_FAILED(x)                                   \
{                                                            \
    HRESULT hr__ = (x);                                      \
    std::wstring wfn = DxUtil::AnsiToWString(__FILE__);      \
    if(FAILED(hr__)) {                                       \
        throw DxUtil::DxException(hr__, L#x, wfn, __LINE__); \
    }                                                        \
}

#define RELEASE_COM(x) \
{                      \
    if(x) {            \
        x->Release();  \
        x = nullptr;   \
    }                  \
}