#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"

struct PerObjectData
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    UINT MaterialIndex;
    UINT ObjPad0;
    UINT ObjPad1;
    UINT ObjPad2;
};

struct FrameConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    Light Lights[MaxLights];
};

struct SkyAtmosphereData
{
    DirectX::XMFLOAT3 SunDirection = { 0.0f, 1.0f, 0.0f };
    float SunIntensity = 22.0f;

    DirectX::XMFLOAT3 RayleighScattering = { 5.8e-6f, 13.5e-6f, 33.1e-6f };
    float PlanetRadius = 6371000.0f;

    DirectX::XMFLOAT3 MieScattering = { 21e-6f, 21e-6f, 21e-6f };
    float AtmosphereRadius = 6471000.0f;

    float RayleighScaleHeight = 8500.0f;
    float MieScaleHeight = 1200.0f;
    float MieAnisotropy = 0.758f;
    float AtmosphereDensity = 1.0f;

    DirectX::XMFLOAT3 CameraPositionKm = { 0.0f, 0.0f, 0.0f };
    float Exposure = 2.0f;

    int NumSamples = 16;
    int NumLightSamples = 8;
    float pad0;
    float pad1;

    DirectX::XMFLOAT3 FogInscatteringColor = { 0.5f, 0.6f, 0.7f };
    float FogDensity = 0.02f;

    float FogHeightFalloff = 0.2f;
    float FogHeight = 0.0f;
    float FogStartDistance = 0.0f;
    float FogCutoffDistance = 1000.0f;

    float FogMaxOpacity = 1.0f;
    int FogEnabled = 1;
    float FogPad0;
    float FogPad1;
};

struct SurfaceProperties
{
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.5f;

    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    UINT DiffuseMapIndex = 0;
    UINT NormalMapIndex = 0;
    UINT MaterialID;
    UINT MaterialPad2;
};

struct MeshVertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 TangentU;
};

struct RenderFrame
{
public:
    RenderFrame(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    RenderFrame(const RenderFrame& rhs) = delete;
    RenderFrame& operator=(const RenderFrame& rhs) = delete;
    ~RenderFrame();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    std::unique_ptr<UploadBuffer<FrameConstants>> FrameBuffer = nullptr;
    std::unique_ptr<UploadBuffer<PerObjectData>> ObjectBuffer = nullptr;
    std::unique_ptr<UploadBuffer<SkyAtmosphereData>> SkyBuffer = nullptr;
    std::unique_ptr<UploadBuffer<SurfaceProperties>> SurfaceBuffer = nullptr;

    UINT64 Fence = 0;
};