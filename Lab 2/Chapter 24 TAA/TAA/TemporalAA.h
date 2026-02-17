#pragma once

#include "../../Common/d3dUtil.h"

class TemporalBuffer
{
public:
    TemporalBuffer(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);

    TemporalBuffer(const TemporalBuffer&) = delete;
    TemporalBuffer& operator=(const TemporalBuffer&) = delete;
    ~TemporalBuffer() = default;

    UINT GetWidth() const;
    UINT GetHeight() const;
    ID3D12Resource* GetCurrent();
    ID3D12Resource* GetArchive();

    CD3DX12_GPU_DESCRIPTOR_HANDLE GetCurrentSRV() const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

    CD3DX12_GPU_DESCRIPTOR_HANDLE GetArchiveSRV() const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetArchiveRTV() const;

    D3D12_VIEWPORT GetDisplayArea() const;
    D3D12_RECT GetClipArea() const;

    void SetupDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurrentSRV,
        CD3DX12_GPU_DESCRIPTOR_HANDLE gpuCurrentSRV,
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurrentRTV,
        UINT srvStep,
        UINT rtvStep);

    void AdjustSize(UINT newWidth, UINT newHeight);

    void Flip();

    static DirectX::XMFLOAT2 CalculateOffset(int frameIndex);

private:
    void ConstructViews();
    void ConstructStorage();

private:
    ID3D12Device* mGraphicsDevice = nullptr;

    D3D12_VIEWPORT mViewportSettings;
    D3D12_RECT mScissorSettings;

    UINT mHorizontalPixels = 0;
    UINT mVerticalPixels = 0;
    DXGI_FORMAT mPixelFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mCPU_CurrentSRV;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mGPU_CurrentSRV;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mCPU_CurrentRTV;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mCPU_ArchiveSRV;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mGPU_ArchiveSRV;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mCPU_ArchiveRTV;

    Microsoft::WRL::ComPtr<ID3D12Resource> mActiveBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHistoryBuffer = nullptr;
};