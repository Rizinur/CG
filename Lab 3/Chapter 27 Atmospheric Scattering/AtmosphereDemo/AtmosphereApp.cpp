#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "Atmosphere.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct SceneEntity
{
    SceneEntity() = default;
    SceneEntity(const SceneEntity& rhs) = delete;

    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = -1;

    SurfaceProperties* MaterialRef = nullptr;
    MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Sky,
    Count
};

class AtmosphericDemo : public D3DApp
{
public:
    AtmosphericDemo(HINSTANCE hInstance);
    AtmosphericDemo(const AtmosphericDemo& rhs) = delete;
    AtmosphericDemo& operator=(const AtmosphericDemo& rhs) = delete;
    ~AtmosphericDemo();

    virtual bool Initialize() override;

private:
    virtual void CreateRtvAndDsvDescriptorHeaps() override;
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateSkyCB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildFogRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void BuildOffscreenBuffer();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<SceneEntity*>& ritems);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<RenderFrame>> mFrameResources;
    RenderFrame* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> mFogRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<SurfaceProperties>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::vector<std::unique_ptr<SceneEntity>> mAllRitems;
    std::vector<SceneEntity*> mRitemLayer[(int)RenderLayer::Count];

    UINT mSkyTexHeapIndex = 0;

    FrameConstants mMainPassCB;
    SkyAtmosphereData mSkyCB;

    Camera mCamera;

    std::unique_ptr<SkyDome> mSkyDome;

    float mSunAngle = 0.8f;
    float mSunAzimuth = 0.0f;
    bool mSunAnimationEnabled = false;
    float mSunAnimationSpeed = 0.3f;

    bool mFogEnabled = true;
    float mFogDensity = 0.02f;
    float mFogHeightFalloff = 0.2f;

    ComPtr<ID3D12Resource> mOffscreenBuffer = nullptr;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mOffscreenRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mOffscreenSrvCpuHandle;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mOffscreenSrvGpuHandle;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mDepthSrvGpuHandle;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        AtmosphericDemo theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

AtmosphericDemo::AtmosphericDemo(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"Atmosphere";
    mLastMousePos.x = 0;
    mLastMousePos.y = 0;
    mSunAnimationEnabled = false;
    mSunAnimationSpeed = 0.3f;
}

AtmosphericDemo::~AtmosphericDemo()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool AtmosphericDemo::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCamera.SetPosition(0.0f, 2.0f, -15.0f);

    mSkyDome = std::make_unique<SkyDome>(md3dDevice.Get(),
        mClientWidth, mClientHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);

    mSkyDome->SetCleanAtmosphere();

    LoadTextures();
    BuildRootSignature();
    BuildFogRootSignature();
    BuildDescriptorHeaps();
    BuildOffscreenBuffer();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    return true;
}

void AtmosphericDemo::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void AtmosphericDemo::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    if (mSkyDome != nullptr)
    {
        mSkyDome->OnResize(mClientWidth, mClientHeight);
    }

    if (md3dDevice != nullptr && mSrvDescriptorHeap != nullptr)
    {
        BuildOffscreenBuffer();
    }
}

void AtmosphericDemo::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    if (mSunAnimationEnabled)
    {
        mSunAngle += mSunAnimationSpeed * gt.DeltaTime();
        if (mSunAngle > XM_2PI)
            mSunAngle -= XM_2PI;
    }

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
    UpdateSkyCB(gt);
}

void AtmosphericDemo::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    CD3DX12_RESOURCE_BARRIER offscreenBarrier1 = CD3DX12_RESOURCE_BARRIER::Transition(mOffscreenBuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &offscreenBarrier1);

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    mCommandList->ClearRenderTargetView(mOffscreenRtvHandle, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &mOffscreenRtvHandle, true, &DepthStencilView());

    auto matBuffer = mCurrFrameResource->SurfaceBuffer->Resource();
    mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

    mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    auto passCB = mCurrFrameResource->FrameBuffer->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto skyCB = mCurrFrameResource->SkyBuffer->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(3, skyCB->GetGPUVirtualAddress());

    mCommandList->SetPipelineState(mPSOs["opaque"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    mCommandList->SetPipelineState(mPSOs["sky"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

    CD3DX12_RESOURCE_BARRIER offscreenBarrier2 = CD3DX12_RESOURCE_BARRIER::Transition(mOffscreenBuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &offscreenBarrier2);

    CD3DX12_RESOURCE_BARRIER depthBarrier1 = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &depthBarrier1);

    CD3DX12_RESOURCE_BARRIER backBufferBarrier1 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &backBufferBarrier1);

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, nullptr);
    mCommandList->SetGraphicsRootSignature(mFogRootSignature.Get());
    mCommandList->SetPipelineState(mPSOs["fog"].Get());

    mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(1, skyCB->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootDescriptorTable(2, mOffscreenSrvGpuHandle);
    mCommandList->SetGraphicsRootDescriptorTable(3, mDepthSrvGpuHandle);

    mCommandList->IASetVertexBuffers(0, 0, nullptr);
    mCommandList->IASetIndexBuffer(nullptr);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawInstanced(3, 1, 0, 0);

    CD3DX12_RESOURCE_BARRIER depthBarrier2 = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &depthBarrier2);

    CD3DX12_RESOURCE_BARRIER backBufferBarrier2 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &backBufferBarrier2);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void AtmosphericDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void AtmosphericDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void AtmosphericDemo::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void AtmosphericDemo::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(20.0f * dt);
    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-20.0f * dt);
    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-20.0f * dt);
    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(20.0f * dt);

    if (GetAsyncKeyState('1') & 0x8000)
        mSkyDome->SetCleanAtmosphere();
    if (GetAsyncKeyState('2') & 0x8000)
        mSkyDome->SetDirtyAtmosphere();
    if (GetAsyncKeyState('3') & 0x8000)
        mSkyDome->SetMarsAtmosphere();
    if (GetAsyncKeyState('4') & 0x8000)
        mSkyDome->SetSunsetAtmosphere();

    if (GetAsyncKeyState('F') & 0x8000)
        mFogDensity = max(0.001f, mFogDensity - 0.02f * dt);
    if (GetAsyncKeyState('G') & 0x8000)
        mFogDensity = min(0.5f, mFogDensity + 0.02f * dt);

    static bool tWasPressed = false;
    if (GetAsyncKeyState('T') & 0x8000)
    {
        if (!tWasPressed)
        {
            mFogEnabled = !mFogEnabled;
            tWasPressed = true;
        }
    }
    else
    {
        tWasPressed = false;
    }

    if (GetAsyncKeyState('R') & 0x8000)
        mFogHeightFalloff = max(0.01f, mFogHeightFalloff - 0.1f * dt);
    if (GetAsyncKeyState('Y') & 0x8000)
        mFogHeightFalloff = min(2.0f, mFogHeightFalloff + 0.1f * dt);

    static bool spaceWasPressed = false;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000)
    {
        if (!spaceWasPressed)
        {
            mSunAnimationEnabled = !mSunAnimationEnabled;
            spaceWasPressed = true;
        }
    }
    else
    {
        spaceWasPressed = false;
    }

    mCamera.UpdateViewMatrix();
}

void AtmosphericDemo::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectBuffer.get();
    for (auto& e : mAllRitems)
    {
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            PerObjectData objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            objConstants.MaterialIndex = e->MaterialRef->MaterialID;

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            e->NumFramesDirty--;
        }
    }
}

void AtmosphericDemo::UpdateMaterialBuffer(const GameTimer& gt)
{
    auto currMaterialBuffer = mCurrFrameResource->SurfaceBuffer.get();
    for (auto& e : mMaterials)
    {
        SurfaceProperties* mat = e.second.get();
        if (mat->MaterialID > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            SurfaceProperties matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
            matData.DiffuseMapIndex = mat->DiffuseMapIndex;
            matData.NormalMapIndex = mat->NormalMapIndex;
            matData.MaterialID = mat->MaterialID;

            currMaterialBuffer->CopyData(mat->MaterialID, matData);
        }
    }
}

void AtmosphericDemo::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMVECTOR viewDet = XMMatrixDeterminant(view);
    XMVECTOR projDet = XMMatrixDeterminant(proj);
    XMVECTOR viewProjDet = XMMatrixDeterminant(viewProj);
    XMMATRIX invView = XMMatrixInverse(&viewDet, view);
    XMMATRIX invProj = XMMatrixInverse(&projDet, proj);
    XMMATRIX invViewProj = XMMatrixInverse(&viewProjDet, viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

    // »спользуем фиксированное положение солнца
    float sunY = sinf(mSunAngle);
    float sunXZ = cosf(mSunAngle);

    mMainPassCB.Lights[0].Direction = {
        -sunXZ * sinf(mSunAzimuth),
        -sunY,
        -sunXZ * cosf(mSunAzimuth)
    };
    mMainPassCB.Lights[0].Strength = { 1.0f, 0.95f, 0.8f };

    auto currPassCB = mCurrFrameResource->FrameBuffer.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void AtmosphericDemo::UpdateSkyCB(const GameTimer& gt)
{
    auto& config = mSkyDome->GetConfig();

    // »спользуем фиксированное положение солнца
    float sunY = sinf(mSunAngle);
    float sunXZ = cosf(mSunAngle);
    mSkyCB.SunDirection = { sunXZ * sinf(mSunAzimuth), sunY, sunXZ * cosf(mSunAzimuth) };
    mSkyCB.SunIntensity = config.SunIntensity;

    mSkyCB.RayleighScattering = config.RayleighCoefficients;
    mSkyCB.PlanetRadius = config.PlanetRadius;

    mSkyCB.MieScattering = config.MieCoefficients;
    mSkyCB.AtmosphereRadius = config.PlanetRadius + config.AtmosphereHeight;

    mSkyCB.RayleighScaleHeight = config.RayleighScaleHeight;
    mSkyCB.MieScaleHeight = config.MieScaleHeight;
    mSkyCB.MieAnisotropy = config.MieAnisotropy;
    mSkyCB.AtmosphereDensity = config.DensityMultiplier;

    XMFLOAT3 camPos = mCamera.GetPosition3f();
    mSkyCB.CameraPositionKm = { camPos.x * 0.001f, camPos.y * 0.001f, camPos.z * 0.001f };
    mSkyCB.Exposure = config.Exposure;

    mSkyCB.NumSamples = config.NumViewSamples;
    mSkyCB.NumLightSamples = config.NumLightSamples;

    mSkyCB.FogInscatteringColor = { 0.5f, 0.6f, 0.7f };
    mSkyCB.FogDensity = mFogDensity;
    mSkyCB.FogHeightFalloff = mFogHeightFalloff;
    mSkyCB.FogHeight = 0.0f;
    mSkyCB.FogStartDistance = 0.0f;
    mSkyCB.FogCutoffDistance = 500.0f;
    mSkyCB.FogMaxOpacity = 1.0f;
    mSkyCB.FogEnabled = mFogEnabled ? 1 : 0;

    auto currSkyCB = mCurrFrameResource->SkyBuffer.get();
    currSkyCB->CopyData(0, mSkyCB);
}

void AtmosphericDemo::LoadTextures()
{
    auto whiteTex = std::make_unique<Texture>();
    whiteTex->Name = "whiteTex";
    whiteTex->Filename = L"../../Textures/white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), whiteTex->Filename.c_str(),
        whiteTex->Resource, whiteTex->UploadHeap));
    mTextures[whiteTex->Name] = std::move(whiteTex);

    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"../../Textures/bricks2.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), bricksTex->Filename.c_str(),
        bricksTex->Resource, bricksTex->UploadHeap));
    mTextures[bricksTex->Name] = std::move(bricksTex);

    auto bricksNormalTex = std::make_unique<Texture>();
    bricksNormalTex->Name = "bricksNormalTex";
    bricksNormalTex->Filename = L"../../Textures/bricks2_nmap.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), bricksNormalTex->Filename.c_str(),
        bricksNormalTex->Resource, bricksNormalTex->UploadHeap));
    mTextures[bricksNormalTex->Name] = std::move(bricksNormalTex);

    auto tileTex = std::make_unique<Texture>();
    tileTex->Name = "tileTex";
    tileTex->Filename = L"../../Textures/grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), tileTex->Filename.c_str(),
        tileTex->Resource, tileTex->UploadHeap));
    mTextures[tileTex->Name] = std::move(tileTex);

    auto tileNormalTex = std::make_unique<Texture>();
    tileNormalTex->Name = "tileNormalTex";
    tileNormalTex->Filename = L"../../Textures/tile_nmap.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), tileNormalTex->Filename.c_str(),
        tileNormalTex->Resource, tileNormalTex->UploadHeap));
    mTextures[tileNormalTex->Name] = std::move(tileNormalTex);

    auto defaultNormalTex = std::make_unique<Texture>();
    defaultNormalTex->Name = "defaultNormalTex";
    defaultNormalTex->Filename = L"../../Textures/default_nmap.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), defaultNormalTex->Filename.c_str(),
        defaultNormalTex->Resource, defaultNormalTex->UploadHeap));
    mTextures[defaultNormalTex->Name] = std::move(defaultNormalTex);
}

void AtmosphericDemo::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[5];

    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 1);
    slotRootParameter[3].InitAsConstantBufferView(2);
    slotRootParameter[4].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void AtmosphericDemo::BuildFogRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE sceneTexTable;
    sceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE depthTexTable;
    depthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    slotRootParameter[0].InitAsConstantBufferView(1);
    slotRootParameter[1].InitAsConstantBufferView(2);
    slotRootParameter[2].InitAsDescriptorTable(1, &sceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[3].InitAsDescriptorTable(1, &depthTexTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mFogRootSignature.GetAddressOf())));
}

void AtmosphericDemo::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 12;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto whiteTex = mTextures["whiteTex"]->Resource;
    auto bricksTex = mTextures["bricksTex"]->Resource;
    auto bricksNormalTex = mTextures["bricksNormalTex"]->Resource;
    auto tileTex = mTextures["tileTex"]->Resource;
    auto tileNormalTex = mTextures["tileNormalTex"]->Resource;
    auto defaultNormalTex = mTextures["defaultNormalTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    srvDesc.Format = whiteTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = whiteTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(whiteTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = bricksTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = bricksNormalTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = bricksNormalTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(bricksNormalTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = tileTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = tileNormalTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tileNormalTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(tileNormalTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = defaultNormalTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = defaultNormalTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(defaultNormalTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    mOffscreenSrvCpuHandle = hDescriptor;
    mOffscreenSrvGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 6, mCbvSrvUavDescriptorSize);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    mDepthSrvGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 7, mCbvSrvUavDescriptorSize);
}

void AtmosphericDemo::BuildOffscreenBuffer()
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mClientWidth;
    texDesc.Height = mClientHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = mBackBufferFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mBackBufferFormat;
    optClear.Color[0] = 0.0f;
    optClear.Color[1] = 0.0f;
    optClear.Color[2] = 0.0f;
    optClear.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &optClear,
        IID_PPV_ARGS(&mOffscreenBuffer)));

    mOffscreenRtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        SwapChainBufferCount,
        mRtvDescriptorSize);
    md3dDevice->CreateRenderTargetView(mOffscreenBuffer.Get(), nullptr, mOffscreenRtvHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mBackBufferFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    md3dDevice->CreateShaderResourceView(mOffscreenBuffer.Get(), &srvDesc, mOffscreenSrvCpuHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    depthSrvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle(
        mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 7, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(mDepthStencilBuffer.Get(), &depthSrvDesc, depthSrvCpuHandle);
}

void AtmosphericDemo::BuildShadersAndInputLayout()
{
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["fogVS"] = d3dUtil::CompileShader(L"Shaders\\Fog.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["fogPS"] = d3dUtil::CompileShader(L"Shaders\\Fog.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void AtmosphericDemo::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(5.0f, 5.0f, 50, 50);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
    GeometryGenerator::MeshData skySphere = geoGen.CreateSphere(500.0f, 30, 30);

    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
    UINT skySphereVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
    UINT skySphereIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    SubmeshGeometry skySphereSubmesh;
    skySphereSubmesh.IndexCount = (UINT)skySphere.Indices32.size();
    skySphereSubmesh.StartIndexLocation = skySphereIndexOffset;
    skySphereSubmesh.BaseVertexLocation = skySphereVertexOffset;

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size() +
        skySphere.Vertices.size();

    std::vector<MeshVertex> vertices(totalVertexCount);

    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
        vertices[k].TangentU = box.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
        vertices[k].TangentU = grid.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
        vertices[k].TangentU = sphere.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
        vertices[k].TangentU = cylinder.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < skySphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = skySphere.Vertices[i].Position;
        vertices[k].Normal = skySphere.Vertices[i].Normal;
        vertices[k].TexC = skySphere.Vertices[i].TexC;
        vertices[k].TangentU = skySphere.Vertices[i].TangentU;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
    indices.insert(indices.end(), std::begin(skySphere.GetIndices16()), std::end(skySphere.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(MeshVertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(MeshVertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;
    geo->DrawArgs["skySphere"] = skySphereSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void AtmosphericDemo::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;
    skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
        mShaders["skyVS"]->GetBufferSize()
    };
    skyPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
        mShaders["skyPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC fogPsoDesc = opaquePsoDesc;
    fogPsoDesc.pRootSignature = mFogRootSignature.Get();
    fogPsoDesc.InputLayout = { nullptr, 0 };
    fogPsoDesc.DepthStencilState.DepthEnable = FALSE;
    fogPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    fogPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    fogPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["fogVS"]->GetBufferPointer()),
        mShaders["fogVS"]->GetBufferSize()
    };
    fogPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["fogPS"]->GetBufferPointer()),
        mShaders["fogPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&fogPsoDesc, IID_PPV_ARGS(&mPSOs["fog"])));
}

void AtmosphericDemo::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<RenderFrame>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void AtmosphericDemo::BuildMaterials()
{
    auto bricks = std::make_unique<SurfaceProperties>();
    bricks->MaterialID = 0;
    bricks->DiffuseMapIndex = 1;
    bricks->NormalMapIndex = 2;
    bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks->Roughness = 0.3f;
    mMaterials["bricks"] = std::move(bricks);

    auto tile = std::make_unique<SurfaceProperties>();
    tile->MaterialID = 1;
    tile->DiffuseMapIndex = 3;
    tile->NormalMapIndex = 5;
    tile->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
    tile->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    tile->Roughness = 0.1f;
    mMaterials["tile"] = std::move(tile);

    auto white = std::make_unique<SurfaceProperties>();
    white->MaterialID = 2;
    white->DiffuseMapIndex = 0;
    white->NormalMapIndex = 5;
    white->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    white->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    white->Roughness = 0.5f;
    mMaterials["white"] = std::move(white);

    auto sky = std::make_unique<SurfaceProperties>();
    sky->MaterialID = 3;
    sky->DiffuseMapIndex = 0;
    sky->NormalMapIndex = 5;
    sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    sky->Roughness = 1.0f;
    mMaterials["sky"] = std::move(sky);

    auto darkGray = std::make_unique<SurfaceProperties>();
    darkGray->MaterialID = 4;
    darkGray->DiffuseMapIndex = 0;
    darkGray->NormalMapIndex = 5;
    darkGray->DiffuseAlbedo = XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f);
    darkGray->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    darkGray->Roughness = 0.9f;
    mMaterials["darkGray"] = std::move(darkGray);
}

void AtmosphericDemo::BuildRenderItems()
{
    auto skyRitem = std::make_unique<SceneEntity>();
    XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    skyRitem->TexTransform = MathHelper::Identity4x4();
    skyRitem->ObjCBIndex = 0;
    skyRitem->MaterialRef = mMaterials["sky"].get();
    skyRitem->Geo = mGeometries["shapeGeo"].get();
    skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skyRitem->IndexCount = skyRitem->Geo->DrawArgs["skySphere"].IndexCount;
    skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["skySphere"].StartIndexLocation;
    skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["skySphere"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
    mAllRitems.push_back(std::move(skyRitem));

    auto sphereRitem = std::make_unique<SceneEntity>();
    XMMATRIX sphereWorld = XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(0.0f, 2.0f, 0.0f);
    XMStoreFloat4x4(&sphereRitem->World, sphereWorld);
    sphereRitem->TexTransform = MathHelper::Identity4x4();
    sphereRitem->ObjCBIndex = 1;
    sphereRitem->MaterialRef = mMaterials["white"].get();
    sphereRitem->Geo = mGeometries["shapeGeo"].get();
    sphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem.get());
    mAllRitems.push_back(std::move(sphereRitem));

    auto gridRitem = std::make_unique<SceneEntity>();
    XMMATRIX gridWorld = XMMatrixScaling(4.5f, 1.0f, 4.5f);
    XMStoreFloat4x4(&gridRitem->World, gridWorld);
    gridRitem->TexTransform = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 2;
    gridRitem->MaterialRef = mMaterials["darkGray"].get();
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
    mAllRitems.push_back(std::move(gridRitem));
}

void AtmosphericDemo::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<SceneEntity*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PerObjectData));

    auto objectCB = mCurrFrameResource->ObjectBuffer->Resource();

    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        D3D12_VERTEX_BUFFER_VIEW vbv = ri->Geo->VertexBufferView();
        D3D12_INDEX_BUFFER_VIEW ibv = ri->Geo->IndexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> AtmosphericDemo::GetStaticSamplers()
{
    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        8);

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f,
        8);

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}