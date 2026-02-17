#include "TemporalAA.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

TemporalBuffer::TemporalBuffer(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
    mGraphicsDevice = device;
    mHorizontalPixels = width;
    mVerticalPixels = height;
    mPixelFormat = format;

    mViewportSettings = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    mScissorSettings = { 0, 0, (int)width, (int)height };

    ConstructStorage();
}

UINT TemporalBuffer::GetWidth() const
{
    return mHorizontalPixels;
}

UINT TemporalBuffer::GetHeight() const
{
    return mVerticalPixels;
}

ID3D12Resource* TemporalBuffer::GetCurrent()
{
    return mActiveBuffer.Get();
}

ID3D12Resource* TemporalBuffer::GetArchive()
{
    return mHistoryBuffer.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE TemporalBuffer::GetCurrentSRV() const
{
    return mGPU_CurrentSRV;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE TemporalBuffer::GetCurrentRTV() const
{
    return mCPU_CurrentRTV;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE TemporalBuffer::GetArchiveSRV() const
{
    return mGPU_ArchiveSRV;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE TemporalBuffer::GetArchiveRTV() const
{
    return mCPU_ArchiveRTV;
}

D3D12_VIEWPORT TemporalBuffer::GetDisplayArea() const
{
    return mViewportSettings;
}

D3D12_RECT TemporalBuffer::GetClipArea() const
{
    return mScissorSettings;
}

void TemporalBuffer::SetupDescriptors(
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurrentSRV,
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuCurrentSRV,
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurrentRTV,
    UINT srvStep,
    UINT rtvStep)
{
    mCPU_CurrentSRV = cpuCurrentSRV;
    mGPU_CurrentSRV = gpuCurrentSRV;
    mCPU_CurrentRTV = cpuCurrentRTV;

    mCPU_ArchiveSRV = cpuCurrentSRV.Offset(1, srvStep);
    mGPU_ArchiveSRV = gpuCurrentSRV.Offset(1, srvStep);
    mCPU_ArchiveRTV = cpuCurrentRTV.Offset(1, rtvStep);

    ConstructViews();
}

void TemporalBuffer::AdjustSize(UINT newWidth, UINT newHeight)
{
    if ((mHorizontalPixels != newWidth) || (mVerticalPixels != newHeight))
    {
        mHorizontalPixels = newWidth;
        mVerticalPixels = newHeight;

        mViewportSettings = { 0.0f, 0.0f, (float)newWidth, (float)newHeight, 0.0f, 1.0f };
        mScissorSettings = { 0, 0, (int)newWidth, (int)newHeight };

        ConstructStorage();
    }
}

void TemporalBuffer::Flip()
{
}

XMFLOAT2 TemporalBuffer::CalculateOffset(int frameIndex)
{
    static const XMFLOAT2 haltonPattern[8] = {
        XMFLOAT2(0.5f, 0.333333f),
        XMFLOAT2(0.25f, 0.666667f),
        XMFLOAT2(0.75f, 0.111111f),
        XMFLOAT2(0.125f, 0.444444f),
        XMFLOAT2(0.625f, 0.777778f),
        XMFLOAT2(0.375f, 0.222222f),
        XMFLOAT2(0.875f, 0.555556f),
        XMFLOAT2(0.0625f, 0.888889f)
    };

    int selection = frameIndex % 8;

    return XMFLOAT2(
        haltonPattern[selection].x - 0.5f,
        haltonPattern[selection].y - 0.5f
    );
}

void TemporalBuffer::ConstructViews()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvSpec = {};
    srvSpec.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvSpec.Format = mPixelFormat;
    srvSpec.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvSpec.Texture2D.MostDetailedMip = 0;
    srvSpec.Texture2D.MipLevels = 1;

    D3D12_RENDER_TARGET_VIEW_DESC rtvSpec = {};
    rtvSpec.Format = mPixelFormat;
    rtvSpec.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvSpec.Texture2D.MipSlice = 0;

    mGraphicsDevice->CreateShaderResourceView(mActiveBuffer.Get(), &srvSpec, mCPU_CurrentSRV);
    mGraphicsDevice->CreateRenderTargetView(mActiveBuffer.Get(), &rtvSpec, mCPU_CurrentRTV);

    mGraphicsDevice->CreateShaderResourceView(mHistoryBuffer.Get(), &srvSpec, mCPU_ArchiveSRV);
    mGraphicsDevice->CreateRenderTargetView(mHistoryBuffer.Get(), &rtvSpec, mCPU_ArchiveRTV);
}

void TemporalBuffer::ConstructStorage()
{
    D3D12_RESOURCE_DESC textureLayout = {};
    textureLayout.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureLayout.Alignment = 0;
    textureLayout.Width = mHorizontalPixels;
    textureLayout.Height = mVerticalPixels;
    textureLayout.DepthOrArraySize = 1;
    textureLayout.MipLevels = 1;
    textureLayout.Format = mPixelFormat;
    textureLayout.SampleDesc.Count = 1;
    textureLayout.SampleDesc.Quality = 0;
    textureLayout.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureLayout.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float resetColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    CD3DX12_CLEAR_VALUE clearSetup(mPixelFormat, resetColor);

    ThrowIfFailed(mGraphicsDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureLayout,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &clearSetup,
        IID_PPV_ARGS(&mActiveBuffer)));

    ThrowIfFailed(mGraphicsDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureLayout,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &clearSetup,
        IID_PPV_ARGS(&mHistoryBuffer)));
}