#include "MotionVectors.h"

using Microsoft::WRL::ComPtr;

VelocityMap::VelocityMap(ID3D12Device* device, UINT width, UINT height)
{
    mDeviceRef = device;
    mDimensionX = width;
    mDimensionY = height;

    GenerateBuffer();
}

UINT VelocityMap::Width() const
{
    return mDimensionX;
}

UINT VelocityMap::Height() const
{
    return mDimensionY;
}

ID3D12Resource* VelocityMap::GetData()
{
    return mVelocityStorage.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE VelocityMap::GetSRV() const
{
    return mGPU_SRV;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE VelocityMap::GetRTV() const
{
    return mCPU_RTV;
}

void VelocityMap::CreateViews(
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSRV,
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSRV,
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRTV)
{
    mCPU_SRV = cpuSRV;
    mGPU_SRV = gpuSRV;
    mCPU_RTV = cpuRTV;

    GenerateViews();
}

void VelocityMap::UpdateSize(UINT newWidth, UINT newHeight)
{
    if ((mDimensionX != newWidth) || (mDimensionY != newHeight))
    {
        mDimensionX = newWidth;
        mDimensionY = newHeight;

        GenerateBuffer();
        GenerateViews();
    }
}

void VelocityMap::GenerateViews()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvSetup = {};
    srvSetup.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvSetup.Format = mStorageFormat;
    srvSetup.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvSetup.Texture2D.MostDetailedMip = 0;
    srvSetup.Texture2D.MipLevels = 1;

    D3D12_RENDER_TARGET_VIEW_DESC rtvSetup = {};
    rtvSetup.Format = mStorageFormat;
    rtvSetup.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvSetup.Texture2D.MipSlice = 0;

    mDeviceRef->CreateShaderResourceView(mVelocityStorage.Get(), &srvSetup, mCPU_SRV);
    mDeviceRef->CreateRenderTargetView(mVelocityStorage.Get(), &rtvSetup, mCPU_RTV);
}

void VelocityMap::GenerateBuffer()
{
    D3D12_RESOURCE_DESC bufferLayout = {};
    bufferLayout.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    bufferLayout.Alignment = 0;
    bufferLayout.Width = mDimensionX;
    bufferLayout.Height = mDimensionY;
    bufferLayout.DepthOrArraySize = 1;
    bufferLayout.MipLevels = 1;
    bufferLayout.Format = mStorageFormat;
    bufferLayout.SampleDesc.Count = 1;
    bufferLayout.SampleDesc.Quality = 0;
    bufferLayout.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    bufferLayout.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float resetColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    CD3DX12_CLEAR_VALUE clearSetup(mStorageFormat, resetColor);

    ThrowIfFailed(mDeviceRef->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &bufferLayout,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &clearSetup,
        IID_PPV_ARGS(&mVelocityStorage)));
}