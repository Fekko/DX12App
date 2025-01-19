#include "DxUtil.h"

#include <fstream>
#include <vector>
#include <d3dcompiler.h>
#include <comdef.h>

using namespace Microsoft::WRL;
using namespace DxUtil;

UINT DxUtil::CalcConstantBufferByteSize(UINT byteSize) {
    // Constant buffers must be a multiple of the minimum hardware
    // allocation size (usually 256 bytes).  So round up to nearest
    // multiple of 256.  We do this by adding 255 and then masking off
    // the lower 2 bytes which store all bits < 256.
    // Example: Suppose byteSize = 300.
    // (300 + 255) & ~255
    // 555 & ~255
    // 0x022B & ~0x00ff
    // 0x022B & 0xff00
    // 0x0200
    // 512
    return (byteSize + 255) & ~255;
}

FileNotFoundException::FileNotFoundException(const std::wstring& filename) :
    Filename{filename}
{};

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode{ hr },
    FunctionName{ functionName },
    Filename{ filename },
    LineNumber{ lineNumber } {}

void DxUtil::LogAdapterOutputs(
    IDXGIAdapter* adapter) 
{
    UINT i{};
    IDXGIOutput* output{};
    while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND) {
        DXGI_OUTPUT_DESC desc{};
        output->GetDesc(&desc);

        std::wstring text = L"***Output: ";
        text += desc.DeviceName;
        text += L"\n";

        OutputDebugString(text.c_str());
        LogOutputDisplayModes(output, DXGI_FORMAT_B8G8R8A8_UNORM);
        RELEASE_COM(output);
        ++i;
    }
}

void DxUtil::LogOutputDisplayModes(
    IDXGIOutput* output, 
    DXGI_FORMAT format) 
{
    UINT count{};
    UINT flags{};

    // Call with nullptr to get list count.
    output->GetDisplayModeList(format, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modeList(count);
    output->GetDisplayModeList(format, flags, &count, &modeList[0]);

    for (auto& x : modeList) {
        UINT n = x.RefreshRate.Numerator;
        UINT d = x.RefreshRate.Denominator;
        std::wstring text =
            L"Width = " + std::to_wstring(x.Width) + L" " +
            L"Height = " + std::to_wstring(x.Height) + L" " +
            L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
            L"\n";

        OutputDebugString(text.c_str());
    }
}

ComPtr<ID3D12Resource> DxUtil::CreateDefaultBuffer(
    ID3D12Device* pDevice, 
    ID3D12GraphicsCommandList* pCommandList, 
    const void* pInitData, 
    UINT64 byteSize, 
    ComPtr<ID3D12Resource>& pUploadBuffer) 
{
#pragma region Create Buffers
    auto heapProperties1 = D3D12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto resourceDesc1 = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    ComPtr<ID3D12Resource> pDefaultBuffer{};
    THROW_IF_FAILED(pDevice->CreateCommittedResource(
        &heapProperties1,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc1,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(pDefaultBuffer.GetAddressOf())));

    auto heapProperties2 = D3D12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc2 = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    THROW_IF_FAILED(pDevice->CreateCommittedResource(
        &heapProperties2,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc2,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(pUploadBuffer.GetAddressOf())
        ));
#pragma endregion

#pragma region Upload to default buffer
    D3D12_SUBRESOURCE_DATA subResourceData{
        .pData = pInitData,
        .RowPitch = (LONG_PTR) byteSize,
        .SlicePitch = (LONG_PTR) byteSize,
    };


    auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
        pDefaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    pCommandList->ResourceBarrier(1, &barrier1);

    UpdateSubresources<1>(pCommandList,
            pDefaultBuffer.Get(),
            pUploadBuffer.Get(), 
            0, 
            0, 
            1, 
            &subResourceData);

    auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
        pDefaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    pCommandList->ResourceBarrier(1, &barrier2);
#pragma endregion

    return pDefaultBuffer;
}
 

ComPtr<ID3DBlob> DxUtil::LoadBinary(const std::wstring& filename) {
    std::ifstream fin{};

    fin.open(filename, std::ios::binary);

    if (fin.fail()) {
        throw FileNotFoundException(filename);
    }

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int) fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> pBlob{};
    THROW_IF_FAILED(D3DCreateBlob(size, pBlob.GetAddressOf()));

    fin.read((char*)pBlob->GetBufferPointer(), size);
    fin.close();
    return pBlob;
}

std::wstring DxException::ToString() const {
    // Get the string description of the error code.
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}