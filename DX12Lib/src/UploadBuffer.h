#pragma once

#include <wrl.h>

#include "Dxutil.h"
#include "MathHelper.h"

template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(
		ID3D12Device* pDevice, 
		UINT elementCount,
		bool isConstantBuffer) : 
		_isConstantBuffer(isConstantBuffer)
	{
		// Constant buffer elements need to be multiples of 256 bytes.
		// This is because the hardware can only view constant data 
		// at m*256 byte offsets and of n*256 byte lengths. 
		_elementByteSize = (_isConstantBuffer)
			? DxUtil::CalcConstantBufferByteSize(sizeof(T))
			: sizeof(T);

		CD3DX12_HEAP_PROPERTIES prop(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(_elementByteSize * elementCount);

		THROW_IF_FAILED(pDevice->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&_pUploadBuffer))
		);

		THROW_IF_FAILED(_pUploadBuffer->Map(
			0, 
			nullptr, 
			reinterpret_cast<void**>(&_pMappedData))
		);

		// We do not need to unmap until we are done with the resource.  However, we must not write to
		// the resource while it is in use by the GPU (so we must use synchronization techniques).
	}

	~UploadBuffer() {
		if (_pUploadBuffer) {
			_pUploadBuffer->Unmap(0, nullptr);
		}

		_pUploadBuffer = nullptr;
		_pMappedData = nullptr;
	}

	UploadBuffer(const UploadBuffer&) = delete;
	UploadBuffer& operator=(const UploadBuffer&) = delete;

	ID3D12Resource* Resource() const {
		return _pUploadBuffer.Get();
	}

	void CopyData(int elementIndex, const T& data) {
		memcpy(&_pMappedData[elementIndex*_elementByteSize], &data, sizeof(T));
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> _pUploadBuffer{};
	BYTE* _pMappedData{};
	UINT _elementByteSize{};
	bool _isConstantBuffer{};
};
