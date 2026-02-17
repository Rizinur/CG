#pragma once

#include "../../Common/d3dUtil.h"

class VelocityMap
{
public:
    VelocityMap(ID3D12Device* device, UINT width, UINT height);

    VelocityMap(const VelocityMap&) = delete;
    VelocityMap& operator=(const VelocityMap&) = delete;
    ~VelocityMap() = default;

    UINT Width() const;
    UINT Height() const;
    ID3D12Resource* GetData();

    CD3DX12_GPU_DESCRIPTOR_HANDLE GetSRV() const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRTV() const;

    void CreateViews(
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSRV,
        CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSRV,
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRTV);

    void UpdateSize(UINT newWidth, UINT newHeight);

private:
    void GenerateViews();
    void GenerateBuffer();

private:
    ID3D12Device* mDeviceRef = nullptr;

    UINT mDimensionX = 0;
    UINT mDimensionY = 0;
    DXGI_FORMAT mStorageFormat = DXGI_FORMAT_R16G16_FLOAT;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mCPU_SRV;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mGPU_SRV;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mCPU_RTV;

    Microsoft::WRL::ComPtr<ID3D12Resource> mVelocityStorage = nullptr;
};