#pragma once

#include "../../Common/d3dUtil.h"

class SkyDome
{
public:
    SkyDome(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
    SkyDome(const SkyDome& rhs) = delete;
    SkyDome& operator=(const SkyDome& rhs) = delete;
    ~SkyDome() = default;

    ID3D12Resource* Resource() { return mSkyTexture.Get(); }
    CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const { return mGpuShaderView; }
    CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv() const { return mCpuRenderView; }

    D3D12_VIEWPORT Viewport() const { return mViewport; }
    D3D12_RECT ScissorRect() const { return mScissorRect; }

    UINT Width() const { return mWidth; }
    UINT Height() const { return mHeight; }

    void BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv);

    void OnResize(UINT newWidth, UINT newHeight);

    struct SkyConfig
    {
        DirectX::XMFLOAT3 SunDirection = { 0.0f, 0.707f, 0.707f };
        float SunIntensity = 20.0f;

        DirectX::XMFLOAT3 RayleighCoefficients = { 5.8e-6f, 13.5e-6f, 33.1e-6f };
        float RayleighScaleHeight = 8500.0f;

        DirectX::XMFLOAT3 MieCoefficients = { 21e-6f, 21e-6f, 21e-6f };
        float MieScaleHeight = 1200.0f;
        float MieAnisotropy = 0.76f;

        float PlanetRadius = 6371.0f;
        float AtmosphereHeight = 100.0f;

        float DensityMultiplier = 1.0f;

        float Exposure = 1.5f;
        int NumViewSamples = 16;
        int NumLightSamples = 8;
    };

    SkyConfig& GetConfig() { return mActiveConfig; }
    const SkyConfig& GetConfig() const { return mActiveConfig; }

    void SetCleanAtmosphere();
    void SetDirtyAtmosphere();
    void SetMarsAtmosphere();
    void SetSunsetAtmosphere();

private:
    void BuildResource();

private:
    ID3D12Device* md3dDevice = nullptr;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;

    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuShaderView = {};
    CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuShaderView = {};
    CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuRenderView = {};

    bool mDescriptorsInitialized = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> mSkyTexture = nullptr;

    SkyConfig mActiveConfig;
};