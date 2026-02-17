#include "Atmosphere.h"

SkyDome::SkyDome(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
    md3dDevice = device;
    mWidth = width;
    mHeight = height;
    mFormat = format;

    mViewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    mScissorRect = { 0, 0, (int)width, (int)height };

    BuildResource();
}

void SkyDome::BuildDescriptors(
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv)
{
    mCpuShaderView = hCpuSrv;
    mGpuShaderView = hGpuSrv;
    mCpuRenderView = hCpuRtv;
    mDescriptorsInitialized = true;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    md3dDevice->CreateShaderResourceView(mSkyTexture.Get(), &srvDesc, mCpuShaderView);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = mFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    md3dDevice->CreateRenderTargetView(mSkyTexture.Get(), &rtvDesc, mCpuRenderView);
}

void SkyDome::OnResize(UINT newWidth, UINT newHeight)
{
    if ((mWidth != newWidth) || (mHeight != newHeight))
    {
        mWidth = newWidth;
        mHeight = newHeight;

        mViewport = { 0.0f, 0.0f, (float)newWidth, (float)newHeight, 0.0f, 1.0f };
        mScissorRect = { 0, 0, (int)newWidth, (int)newHeight };

        BuildResource();

        if (mDescriptorsInitialized)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = mFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            md3dDevice->CreateShaderResourceView(mSkyTexture.Get(), &srvDesc, mCpuShaderView);

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Format = mFormat;
            rtvDesc.Texture2D.MipSlice = 0;
            md3dDevice->CreateRenderTargetView(mSkyTexture.Get(), &rtvDesc, mCpuRenderView);
        }
    }
}

void SkyDome::BuildResource()
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mWidth;
    texDesc.Height = mHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = mFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mFormat;
    optClear.Color[0] = 0.0f;
    optClear.Color[1] = 0.0f;
    optClear.Color[2] = 0.0f;
    optClear.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &optClear,
        IID_PPV_ARGS(&mSkyTexture)));
}

void SkyDome::SetCleanAtmosphere()
{
    mActiveConfig.DensityMultiplier = 1.0f;
    mActiveConfig.MieAnisotropy = 0.76f;
    mActiveConfig.SunIntensity = 20.0f;
    mActiveConfig.Exposure = 1.5f;
}

void SkyDome::SetDirtyAtmosphere()
{
    mActiveConfig.DensityMultiplier = 3.0f;
    mActiveConfig.MieAnisotropy = 0.6f;
    mActiveConfig.SunIntensity = 18.0f;
    mActiveConfig.Exposure = 1.2f;
}

void SkyDome::SetMarsAtmosphere()
{
    mActiveConfig.DensityMultiplier = 0.3f;
    mActiveConfig.MieAnisotropy = 0.8f;
    mActiveConfig.SunIntensity = 15.0f;
    mActiveConfig.Exposure = 2.0f;
}

void SkyDome::SetSunsetAtmosphere()
{
    mActiveConfig.DensityMultiplier = 2.0f;
    mActiveConfig.MieAnisotropy = 0.85f;
    mActiveConfig.SunIntensity = 25.0f;
    mActiveConfig.Exposure = 1.8f;
}