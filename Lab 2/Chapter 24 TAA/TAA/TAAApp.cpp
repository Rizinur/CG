#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "TemporalAA.h"
#include "MotionVectors.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct AssetMaterial
{
    std::string Name;
    int BufferIndex = -1;
    int DiffuseSlot = -1;
    int NormalSlot = -1;
    int DirtyFrames = gNumFrameResources;

    DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 Reflectance = { 0.01f, 0.01f, 0.01f };
    float Smoothness = 0.25f;
    DirectX::XMFLOAT4X4 UVTransform = MathHelper::Identity4x4();
};

struct TextureAsset
{
    std::string Name;
    std::wstring Path;
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

struct SceneObject
{
    SceneObject() = default;
    SceneObject(const SceneObject&) = delete;

    XMFLOAT4X4 Transform = MathHelper::Identity4x4();
    XMFLOAT4X4 PreviousTransform = MathHelper::Identity4x4();
    XMFLOAT4X4 TextureMatrix = MathHelper::Identity4x4();
    int DirtyCounter = gNumFrameResources;
    UINT ConstantIndex = -1;
    AssetMaterial* Material = nullptr;
    MeshGeometry* Geometry = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT IndexStart = 0;
    int VertexBase = 0;
};

enum class RenderPass : int
{
    Main = 0,
    Count
};

class TemporalDemo : public D3DApp
{
public:
    TemporalDemo(HINSTANCE instance);
    TemporalDemo(const TemporalDemo&) = delete;
    TemporalDemo& operator=(const TemporalDemo&) = delete;
    ~TemporalDemo();

    virtual bool Initialize() override;

private:
    virtual void CreateRtvAndDsvDescriptorHeaps() override;
    virtual void OnResize() override;
    virtual void Update(const GameTimer& timer) override;
    virtual void Draw(const GameTimer& timer) override;

    virtual void OnMouseDown(WPARAM state, int x, int y) override;
    virtual void OnMouseUp(WPARAM state, int x, int y) override;
    virtual void OnMouseMove(WPARAM state, int x, int y) override;
    virtual void ProcessInput(const GameTimer& timer);

    void UpdateTransforms(const GameTimer& timer);
    void UpdateObjectBuffers(const GameTimer& timer);
    void UpdateMaterialStorage(const GameTimer& timer);
    void UpdateMainPassData(const GameTimer& timer);
    void UpdateVectorPassData(const GameTimer& timer);
    void UpdateTemporalData(const GameTimer& timer);

    void PrepareTextures();
    void CreateRootSignatures();
    void AllocateDescriptors();
    void CompileShaders();
    void BuildSceneGeometry();
    void CreatePipelineStates();
    void SetupFrameResources();
    void CreateMaterials();
    void PopulateScene();
    void RenderItems(ID3D12GraphicsCommandList* list, const std::vector<SceneObject*>& items);

    void CaptureSceneColor();
    void GenerateVelocityMap();
    void ApplyTemporalFilter();
    void TransferToBackBuffer(ID3D12Resource* source);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrentFrame = nullptr;
    int mFrameIndex = 0;

    ComPtr<ID3D12RootSignature> mMainSignature = nullptr;
    ComPtr<ID3D12RootSignature> mTemporalSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mShaderHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometryMap;
    std::unordered_map<std::string, std::unique_ptr<AssetMaterial>> mMaterialMap;
    std::unordered_map<std::string, std::unique_ptr<TextureAsset>> mTextureMap;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaderCode;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPipelineCache;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mVertexLayout;
    std::vector<std::unique_ptr<SceneObject>> mAllObjects;
    std::vector<SceneObject*> mActivePass[(int)RenderPass::Count];

    PassConstants mMainConstants;
    PassConstants mPreviousConstants;
    TAAConstants mTemporalConstants;

    Camera mViewCamera;

    std::unique_ptr<TemporalBuffer> mTemporalFilter;
    std::unique_ptr<VelocityMap> mVelocityBuffer;

    ComPtr<ID3D12Resource> mColorBuffer;
    ComPtr<ID3D12Resource> mDepthBuffer;

    UINT mColorSRVSlot = 0;
    UINT mColorRTVSlot = 0;
    UINT mVelocitySRVSlot = 0;
    UINT mVelocityRTVSlot = 0;
    UINT mTemporalOutputSlot = 0;
    UINT mTemporalRTVSlot = 0;
    UINT mHistorySRVSlot = 0;
    UINT mHistoryRTVSlot = 0;
    UINT mDepthSRVSlot = 0;

    int mRenderCounter = 0;
    bool mTemporalActive = true;

    POINT mCursorPosition;

    bool mShowMotionVectors = false;
};

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TemporalDemo app(instance);
        if (!app.Initialize())
            return 0;

        return app.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"System Error", MB_OK);
        return 0;
    }
}

TemporalDemo::TemporalDemo(HINSTANCE instance)
    : D3DApp(instance)
{
}

TemporalDemo::~TemporalDemo()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();

    mTemporalFilter.reset();
    mVelocityBuffer.reset();
    mColorBuffer.Reset();
    mDepthBuffer.Reset();
}

bool TemporalDemo::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mViewCamera.SetPosition(0.0f, 8.0f, -12.0f);

    PrepareTextures();
    CreateRootSignatures();
    AllocateDescriptors();
    CompileShaders();
    BuildSceneGeometry();
    CreateMaterials();
    PopulateScene();
    SetupFrameResources();
    CreatePipelineStates();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(lists), lists);

    FlushCommandQueue();

    return true;
}

void TemporalDemo::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc;
    rtvDesc.NumDescriptors = SwapChainBufferCount + 6;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc;
    dsvDesc.NumDescriptors = 2;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void TemporalDemo::OnResize()
{
    D3DApp::OnResize();

    mViewCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    if (mShaderHeap == nullptr)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 16;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mShaderHeap)));
    }

    if (mTemporalFilter != nullptr)
    {
        mTemporalFilter->AdjustSize(mClientWidth, mClientHeight);
        mVelocityBuffer->UpdateSize(mClientWidth, mClientHeight);
    }
    else
    {
        mTemporalFilter = std::make_unique<TemporalBuffer>(
            md3dDevice.Get(), mClientWidth, mClientHeight, mBackBufferFormat);
        mVelocityBuffer = std::make_unique<VelocityMap>(
            md3dDevice.Get(), mClientWidth, mClientHeight);
    }

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = mClientWidth;
    colorDesc.Height = mClientHeight;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = mBackBufferFormat;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float clearValues[] = { 0.1f, 0.15f, 0.2f, 1.0f };
    CD3DX12_CLEAR_VALUE colorClear(mBackBufferFormat, clearValues);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &colorDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &colorClear,
        IID_PPV_ARGS(&mColorBuffer)));

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = mClientWidth;
    depthDesc.Height = mClientHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    CD3DX12_CLEAR_VALUE depthClear(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&mDepthBuffer)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(SwapChainBufferCount, mRtvDescriptorSize);

    mColorRTVSlot = SwapChainBufferCount;
    md3dDevice->CreateRenderTargetView(mColorBuffer.Get(), nullptr, rtvHandle);

    mVelocityRTVSlot = SwapChainBufferCount + 1;
    mTemporalRTVSlot = SwapChainBufferCount + 2;
    mHistoryRTVSlot = SwapChainBufferCount + 3;

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsvHandle.Offset(1, mDsvDescriptorSize);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.Texture2D.MipSlice = 0;
    md3dDevice->CreateDepthStencilView(mDepthBuffer.Get(), &dsvDesc, dsvHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    mColorSRVSlot = 0;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPU(mShaderHeap->GetCPUDescriptorHandleForHeapStart());
    srvCPU.Offset(mColorSRVSlot, mCbvSrvUavDescriptorSize);
    srvDesc.Format = mBackBufferFormat;
    md3dDevice->CreateShaderResourceView(mColorBuffer.Get(), &srvDesc, srvCPU);

    mHistorySRVSlot = 1;
    srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(mShaderHeap->GetCPUDescriptorHandleForHeapStart());
    srvCPU.Offset(mHistorySRVSlot, mCbvSrvUavDescriptorSize);
    srvDesc.Format = mBackBufferFormat;
    md3dDevice->CreateShaderResourceView(mTemporalFilter->GetArchive(), &srvDesc, srvCPU);

    mVelocitySRVSlot = 2;
    srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(mShaderHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGPU(mShaderHeap->GetGPUDescriptorHandleForHeapStart());
    srvCPU.Offset(mVelocitySRVSlot, mCbvSrvUavDescriptorSize);
    srvGPU.Offset(mVelocitySRVSlot, mCbvSrvUavDescriptorSize);
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mVelocityRTVSlot, mRtvDescriptorSize);
    mVelocityBuffer->CreateViews(srvCPU, srvGPU, rtvHandle);

    mDepthSRVSlot = 3;
    srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(mShaderHeap->GetCPUDescriptorHandleForHeapStart());
    srvCPU.Offset(mDepthSRVSlot, mCbvSrvUavDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    md3dDevice->CreateShaderResourceView(mDepthBuffer.Get(), &srvDesc, srvCPU);

    mTemporalOutputSlot = 4;
    srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(mShaderHeap->GetCPUDescriptorHandleForHeapStart());
    srvGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mShaderHeap->GetGPUDescriptorHandleForHeapStart());
    srvCPU.Offset(mTemporalOutputSlot, mCbvSrvUavDescriptorSize);
    srvGPU.Offset(mTemporalOutputSlot, mCbvSrvUavDescriptorSize);
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mTemporalRTVSlot, mRtvDescriptorSize);
    srvDesc.Format = mBackBufferFormat;
    md3dDevice->CreateShaderResourceView(mTemporalFilter->GetCurrent(), &srvDesc, srvCPU);
    md3dDevice->CreateRenderTargetView(mTemporalFilter->GetCurrent(), nullptr, rtvHandle);

    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mHistoryRTVSlot, mRtvDescriptorSize);
    md3dDevice->CreateRenderTargetView(mTemporalFilter->GetArchive(), nullptr, rtvHandle);
}

void TemporalDemo::Update(const GameTimer& timer)
{
    ProcessInput(timer);

    mFrameIndex = (mFrameIndex + 1) % gNumFrameResources;
    mCurrentFrame = mFrameResources[mFrameIndex].get();

    if (mCurrentFrame->Fence != 0 && mFence->GetCompletedValue() < mCurrentFrame->Fence)
    {
        HANDLE event = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFrame->Fence, event));
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }

    UpdateTransforms(timer);
    UpdateObjectBuffers(timer);
    UpdateMaterialStorage(timer);
    UpdateMainPassData(timer);
    UpdateVectorPassData(timer);
    UpdateTemporalData(timer);

    mRenderCounter++;
}

void TemporalDemo::Draw(const GameTimer& timer)
{
    auto allocator = mCurrentFrame->CmdListAlloc;
    ThrowIfFailed(allocator->Reset());

    ThrowIfFailed(mCommandList->Reset(allocator.Get(), mPipelineCache["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    ID3D12DescriptorHeap* heaps[] = { mShaderHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

    CaptureSceneColor();
    GenerateVelocityMap();

    if (mTemporalActive)
    {
        if (mRenderCounter == 0)
        {
            D3D12_RESOURCE_BARRIER barriers[2];
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
                mColorBuffer.Get(),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_COPY_SOURCE);

            barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
                mTemporalFilter->GetArchive(),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_COPY_DEST);

            mCommandList->ResourceBarrier(2, barriers);

            mCommandList->CopyResource(mTemporalFilter->GetArchive(), mColorBuffer.Get());

            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
                mColorBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_GENERIC_READ);

            barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
                mTemporalFilter->GetArchive(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_GENERIC_READ);

            mCommandList->ResourceBarrier(2, barriers);
        }

        ApplyTemporalFilter();

        D3D12_RESOURCE_BARRIER taaBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalFilter->GetCurrent(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_GENERIC_READ);
        mCommandList->ResourceBarrier(1, &taaBarrier);

        TransferToBackBuffer(mTemporalFilter->GetCurrent());

        D3D12_RESOURCE_BARRIER historyBarriers[2];
        historyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalFilter->GetArchive(),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_COPY_DEST);

        historyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalFilter->GetCurrent(),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

        mCommandList->ResourceBarrier(2, historyBarriers);

        mCommandList->CopyResource(mTemporalFilter->GetArchive(), mTemporalFilter->GetCurrent());

        historyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalFilter->GetArchive(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ);

        historyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalFilter->GetCurrent(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_GENERIC_READ);

        mCommandList->ResourceBarrier(2, historyBarriers);
    }
    else
    {
        TransferToBackBuffer(mColorBuffer.Get());
    }

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(lists), lists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrentFrame->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TemporalDemo::CaptureSceneColor()
{
    D3D12_RESOURCE_BARRIER barriers[2];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        mColorBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        mDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    mCommandList->ResourceBarrier(2, barriers);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtv.Offset(mColorRTVSlot, mRtvDescriptorSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsv.Offset(1, mDsvDescriptorSize);

    float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &rtv, true, &dsv);

    mCommandList->SetGraphicsRootSignature(mMainSignature.Get());

    auto passBuffer = mCurrentFrame->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passBuffer->GetGPUVirtualAddress());

    RenderItems(mCommandList.Get(), mActivePass[(int)RenderPass::Main]);

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        mColorBuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        mDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    mCommandList->ResourceBarrier(2, barriers);
}

void TemporalDemo::GenerateVelocityMap()
{
    mCommandList->SetPipelineState(mPipelineCache["motion"].Get());

    D3D12_RESOURCE_BARRIER barriers[2];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        mVelocityBuffer->GetData(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        mDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_DEPTH_READ);

    mCommandList->ResourceBarrier(2, barriers);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtv.Offset(mVelocityRTVSlot, mRtvDescriptorSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsv.Offset(1, mDsvDescriptorSize);

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &rtv, true, &dsv);

    mCommandList->SetGraphicsRootSignature(mMainSignature.Get());

    auto passBuffer = mCurrentFrame->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passBuffer->GetGPUVirtualAddress());

    RenderItems(mCommandList.Get(), mActivePass[(int)RenderPass::Main]);

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        mVelocityBuffer->GetData(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        mDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_READ,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    mCommandList->ResourceBarrier(2, barriers);
}

void TemporalDemo::ApplyTemporalFilter()
{
    mCommandList->SetPipelineState(mPipelineCache["taa"].Get());

    D3D12_RESOURCE_BARRIER taaBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mTemporalFilter->GetCurrent(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &taaBarrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtv.Offset(mTemporalRTVSlot, mRtvDescriptorSize);

    mCommandList->OMSetRenderTargets(1, &rtv, true, nullptr);

    mCommandList->SetGraphicsRootSignature(mTemporalSignature.Get());

    auto taaBuffer = mCurrentFrame->TAACB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(0, taaBuffer->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE srv(mShaderHeap->GetGPUDescriptorHandleForHeapStart());
    srv.Offset(mColorSRVSlot, mCbvSrvUavDescriptorSize);
    mCommandList->SetGraphicsRootDescriptorTable(1, srv);

    mCommandList->IASetVertexBuffers(0, 0, nullptr);
    mCommandList->IASetIndexBuffer(nullptr);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawInstanced(3, 1, 0, 0);
}

void TemporalDemo::TransferToBackBuffer(ID3D12Resource* source)
{
    D3D12_RESOURCE_BARRIER barriers[2];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        source,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_COPY_DEST);

    mCommandList->ResourceBarrier(2, barriers);

    mCommandList->CopyResource(CurrentBackBuffer(), source);

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        source,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PRESENT);

    mCommandList->ResourceBarrier(2, barriers);
}

void TemporalDemo::OnMouseDown(WPARAM state, int x, int y)
{
    mCursorPosition.x = x;
    mCursorPosition.y = y;
    SetCapture(mhMainWnd);
}

void TemporalDemo::OnMouseUp(WPARAM state, int x, int y)
{
    ReleaseCapture();
}

void TemporalDemo::OnMouseMove(WPARAM state, int x, int y)
{
    if ((state & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mCursorPosition.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mCursorPosition.y));

        mViewCamera.Pitch(dy);
        mViewCamera.RotateY(dx);
    }

    mCursorPosition.x = x;
    mCursorPosition.y = y;
}

void TemporalDemo::ProcessInput(const GameTimer& timer)
{
    const float delta = timer.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        mViewCamera.Walk(10.0f * delta);

    if (GetAsyncKeyState('S') & 0x8000)
        mViewCamera.Walk(-10.0f * delta);

    if (GetAsyncKeyState('A') & 0x8000)
        mViewCamera.Strafe(-10.0f * delta);

    if (GetAsyncKeyState('D') & 0x8000)
        mViewCamera.Strafe(10.0f * delta);

    static bool tPressed = false;
    if (GetAsyncKeyState('T') & 0x8000)
    {
        if (!tPressed)
        {
            mTemporalActive = !mTemporalActive;
            tPressed = true;
        }
    }
    else
    {
        tPressed = false;
    }

    static bool rPressed = false;
    if (GetAsyncKeyState('R') & 0x8000)
    {
        if (!rPressed)
        {
            mShowMotionVectors = !mShowMotionVectors;
            rPressed = true;
        }
    }
    else
    {
        rPressed = false;
    }
    mViewCamera.UpdateViewMatrix();
}

void TemporalDemo::UpdateTransforms(const GameTimer& timer)
{
    if (mAllObjects.size() > 0)
    {
        auto& sphere = mAllObjects[1];

        sphere->PreviousTransform = sphere->Transform;

        float time = timer.TotalTime();
        float yPos = 4.0f + sinf(time * 1.5f) * 2.0f;

        XMMATRIX world = XMMatrixTranslation(0.0f, yPos, 0.0f);
        XMStoreFloat4x4(&sphere->Transform, world);

        sphere->DirtyCounter = gNumFrameResources;
    }
}

void TemporalDemo::UpdateObjectBuffers(const GameTimer& timer)
{
    auto objectBuffer = mCurrentFrame->ObjectCB.get();
    for (auto& obj : mAllObjects)
    {
        XMMATRIX world = XMLoadFloat4x4(&obj->Transform);
        XMMATRIX prev = XMLoadFloat4x4(&obj->PreviousTransform);
        XMMATRIX uv = XMLoadFloat4x4(&obj->TextureMatrix);

        ObjectConstants data;
        XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&data.PrevWorld, XMMatrixTranspose(prev));
        XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(uv));
        data.MaterialIndex = obj->Material->BufferIndex;

        objectBuffer->CopyData(obj->ConstantIndex, data);

        if (obj->DirtyCounter > 0)
            obj->DirtyCounter--;
    }
}

void TemporalDemo::UpdateMaterialStorage(const GameTimer& timer)
{
    auto materialBuffer = mCurrentFrame->MaterialBuffer.get();
    for (auto& entry : mMaterialMap)
    {
        AssetMaterial* mat = entry.second.get();
        if (mat->DirtyFrames > 0)
        {
            XMMATRIX uvTransform = XMLoadFloat4x4(&mat->UVTransform);

            MaterialData storage;
            storage.DiffuseAlbedo = mat->BaseColor;
            storage.FresnelR0 = mat->Reflectance;
            storage.Roughness = mat->Smoothness;
            XMStoreFloat4x4(&storage.MatTransform, XMMatrixTranspose(uvTransform));
            storage.DiffuseMapIndex = mat->DiffuseSlot;
            storage.NormalMapIndex = mat->NormalSlot;

            materialBuffer->CopyData(mat->BufferIndex, storage);

            mat->DirtyFrames--;
        }
    }
}

void TemporalDemo::UpdateMainPassData(const GameTimer& timer)
{
    XMFLOAT4X4 previousUnjittered = mMainConstants.UnjitteredViewProj;

    XMMATRIX view = mViewCamera.GetView();
    XMMATRIX proj = mViewCamera.GetProj();

    XMMATRIX unjitteredVP = XMMatrixMultiply(view, proj);

    XMStoreFloat4x4(&mMainConstants.UnjitteredViewProj, XMMatrixTranspose(unjitteredVP));

    if (mRenderCounter > 0)
    {
        mMainConstants.PrevViewProj = previousUnjittered;
    }
    else
    {
        mMainConstants.PrevViewProj = mMainConstants.UnjitteredViewProj;
    }

    if (mTemporalActive)
    {
        XMFLOAT2 jitter = TemporalBuffer::CalculateOffset(mRenderCounter);
        float jitterX = (2.0f * jitter.x) / (float)mClientWidth;
        float jitterY = (2.0f * jitter.y) / (float)mClientHeight;

        XMFLOAT4X4 projMatrix;
        XMStoreFloat4x4(&projMatrix, proj);
        projMatrix._31 += jitterX;
        projMatrix._32 += jitterY;
        proj = XMLoadFloat4x4(&projMatrix);
    }

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainConstants.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainConstants.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainConstants.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainConstants.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainConstants.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainConstants.InvViewProj, XMMatrixTranspose(invViewProj));

    mMainConstants.EyePosW = mViewCamera.GetPosition3f();
    mMainConstants.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainConstants.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainConstants.NearZ = 1.0f;
    mMainConstants.FarZ = 1000.0f;
    mMainConstants.TotalTime = timer.TotalTime();
    mMainConstants.DeltaTime = timer.DeltaTime();

    mMainConstants.AmbientLight = { 0.3f, 0.3f, 0.3f, 1.0f };

    mMainConstants.Lights[0].Direction = { 0.4f, -0.7f, 0.5f };
    mMainConstants.Lights[0].Strength = { 1.2f, 1.2f, 1.2f };

    auto passBuffer = mCurrentFrame->PassCB.get();
    passBuffer->CopyData(0, mMainConstants);
}

void TemporalDemo::UpdateVectorPassData(const GameTimer& timer)
{
    auto passBuffer = mCurrentFrame->PassCB.get();
    passBuffer->CopyData(1, mMainConstants);
}

void TemporalDemo::UpdateTemporalData(const GameTimer& timer)
{
    XMFLOAT2 jitter = TemporalBuffer::CalculateOffset(mRenderCounter);

    mTemporalConstants.JitterOffset = jitter;
    mTemporalConstants.ScreenSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mTemporalConstants.BlendFactor = 0.04f;
    mTemporalConstants.MotionScale = 1.0f;
    mTemporalConstants.MotionDebugEnabled = mShowMotionVectors ? 1.0f : 0.0f;

    auto taaBuffer = mCurrentFrame->TAACB.get();
    taaBuffer->CopyData(0, mTemporalConstants);
}

void TemporalDemo::PrepareTextures()
{
    auto white = std::make_unique<TextureAsset>();
    white->Name = "white";
    white->Path = L"";

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&white->Resource)));

    const UINT64 uploadSize = GetRequiredIntermediateSize(white->Resource.Get(), 0, 1);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&white->UploadHeap)));

    UINT pixel = 0xFFFFFFFF;
    D3D12_SUBRESOURCE_DATA data = {};
    data.pData = &pixel;
    data.RowPitch = 4;
    data.SlicePitch = 4;

    UpdateSubresources(mCommandList.Get(), white->Resource.Get(), white->UploadHeap.Get(),
        0, 0, 1, &data);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        white->Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    mTextureMap[white->Name] = std::move(white);
}

void TemporalDemo::CreateRootSignatures()
{
    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER mainParams[4];
    mainParams[0].InitAsConstantBufferView(0);
    mainParams[1].InitAsConstantBufferView(1);
    mainParams[2].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_PIXEL);
    mainParams[3].InitAsShaderResourceView(1, 1);

    auto samplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC mainDesc(4, mainParams,
        (UINT)samplers.size(), samplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized = nullptr;
    ComPtr<ID3DBlob> error = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&mainDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf());

    if (error != nullptr)
    {
        ::OutputDebugStringA((char*)error->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(mMainSignature.GetAddressOf())));

    CD3DX12_DESCRIPTOR_RANGE taaRange;
    taaRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    CD3DX12_ROOT_PARAMETER taaParams[2];
    taaParams[0].InitAsConstantBufferView(0);
    taaParams[1].InitAsDescriptorTable(1, &taaRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC taaDesc(2, taaParams,
        (UINT)samplers.size(), samplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    serialized = nullptr;
    hr = D3D12SerializeRootSignature(&taaDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf());

    if (error != nullptr)
    {
        ::OutputDebugStringA((char*)error->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(mTemporalSignature.GetAddressOf())));
}

void TemporalDemo::AllocateDescriptors()
{
    if (mShaderHeap == nullptr)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 10;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mShaderHeap)));
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mShaderHeap->GetCPUDescriptorHandleForHeapStart());

    auto whiteTex = mTextureMap["white"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = whiteTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = whiteTex->GetDesc().MipLevels;

    handle.Offset(5, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(whiteTex.Get(), &srvDesc, handle);
}

void TemporalDemo::CompileShaders()
{
    mShaderCode["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaderCode["standardPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

    mShaderCode["velocityVS"] = d3dUtil::CompileShader(L"Shaders\\MotionVectors.hlsl", nullptr, "VS", "vs_5_1");
    mShaderCode["velocityPS"] = d3dUtil::CompileShader(L"Shaders\\MotionVectors.hlsl", nullptr, "PS", "ps_5_1");

    mShaderCode["taaVS"] = d3dUtil::CompileShader(L"Shaders\\TAAResolve.hlsl", nullptr, "VS", "vs_5_1");
    mShaderCode["taaPS"] = d3dUtil::CompileShader(L"Shaders\\TAAResolve.hlsl", nullptr, "PS", "ps_5_1");

    mVertexLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void TemporalDemo::BuildSceneGeometry()
{
    GeometryGenerator gen;
    GeometryGenerator::MeshData box = gen.CreateBox(1.5f, 0.5f, 1.5f, 3);
    GeometryGenerator::MeshData grid = gen.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = gen.CreateSphere(1.2f, 30, 30);
    GeometryGenerator::MeshData cylinder = gen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    SubmeshGeometry boxSub;
    boxSub.IndexCount = (UINT)box.Indices32.size();
    boxSub.StartIndexLocation = boxIndexOffset;
    boxSub.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSub;
    gridSub.IndexCount = (UINT)grid.Indices32.size();
    gridSub.StartIndexLocation = gridIndexOffset;
    gridSub.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSub;
    sphereSub.IndexCount = (UINT)sphere.Indices32.size();
    sphereSub.StartIndexLocation = sphereIndexOffset;
    sphereSub.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSub;
    cylinderSub.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSub.StartIndexLocation = cylinderIndexOffset;
    cylinderSub.BaseVertexLocation = cylinderVertexOffset;

    auto totalVerts =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVerts);

    UINT index = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++index)
    {
        vertices[index].Pos = box.Vertices[i].Position;
        vertices[index].Normal = box.Vertices[i].Normal;
        vertices[index].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++index)
    {
        vertices[index].Pos = grid.Vertices[i].Position;
        vertices[index].Normal = grid.Vertices[i].Normal;
        vertices[index].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++index)
    {
        vertices[index].Pos = sphere.Vertices[i].Position;
        vertices[index].Normal = sphere.Vertices[i].Normal;
        vertices[index].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++index)
    {
        vertices[index].Pos = cylinder.Vertices[i].Position;
        vertices[index].Normal = cylinder.Vertices[i].Normal;
        vertices[index].TexC = cylinder.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geometry = std::make_unique<MeshGeometry>();
    geometry->Name = "scene";

    ThrowIfFailed(D3DCreateBlob(vbSize, &geometry->VertexBufferCPU));
    CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbSize);

    ThrowIfFailed(D3DCreateBlob(ibSize, &geometry->IndexBufferCPU));
    CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibSize);

    geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbSize, geometry->VertexBufferUploader);

    geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibSize, geometry->IndexBufferUploader);

    geometry->VertexByteStride = sizeof(Vertex);
    geometry->VertexBufferByteSize = vbSize;
    geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
    geometry->IndexBufferByteSize = ibSize;

    geometry->DrawArgs["box"] = boxSub;
    geometry->DrawArgs["grid"] = gridSub;
    geometry->DrawArgs["sphere"] = sphereSub;
    geometry->DrawArgs["cylinder"] = cylinderSub;

    mGeometryMap[geometry->Name] = std::move(geometry);
}

void TemporalDemo::CreatePipelineStates()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC baseDesc = {};
    baseDesc.InputLayout = { mVertexLayout.data(), (UINT)mVertexLayout.size() };
    baseDesc.pRootSignature = mMainSignature.Get();
    baseDesc.VS = { reinterpret_cast<BYTE*>(mShaderCode["standardVS"]->GetBufferPointer()), mShaderCode["standardVS"]->GetBufferSize() };
    baseDesc.PS = { reinterpret_cast<BYTE*>(mShaderCode["standardPS"]->GetBufferPointer()), mShaderCode["standardPS"]->GetBufferSize() };
    baseDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    baseDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    baseDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    baseDesc.SampleMask = UINT_MAX;
    baseDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    baseDesc.NumRenderTargets = 1;
    baseDesc.RTVFormats[0] = mBackBufferFormat;
    baseDesc.SampleDesc.Count = 1;
    baseDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&baseDesc, IID_PPV_ARGS(&mPipelineCache["opaque"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC velocityDesc = baseDesc;
    velocityDesc.VS = { reinterpret_cast<BYTE*>(mShaderCode["velocityVS"]->GetBufferPointer()), mShaderCode["velocityVS"]->GetBufferSize() };
    velocityDesc.PS = { reinterpret_cast<BYTE*>(mShaderCode["velocityPS"]->GetBufferPointer()), mShaderCode["velocityPS"]->GetBufferSize() };
    velocityDesc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
    velocityDesc.DepthStencilState.DepthEnable = TRUE;
    velocityDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    velocityDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&velocityDesc, IID_PPV_ARGS(&mPipelineCache["motion"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC taaDesc = baseDesc;
    taaDesc.pRootSignature = mTemporalSignature.Get();
    taaDesc.InputLayout = { nullptr, 0 };
    taaDesc.VS = { reinterpret_cast<BYTE*>(mShaderCode["taaVS"]->GetBufferPointer()), mShaderCode["taaVS"]->GetBufferSize() };
    taaDesc.PS = { reinterpret_cast<BYTE*>(mShaderCode["taaPS"]->GetBufferPointer()), mShaderCode["taaPS"]->GetBufferSize() };
    taaDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    taaDesc.DepthStencilState.DepthEnable = FALSE;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&taaDesc, IID_PPV_ARGS(&mPipelineCache["taa"])));
}

void TemporalDemo::SetupFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            2, (UINT)mAllObjects.size(), (UINT)mMaterialMap.size()));
    }
}

void TemporalDemo::CreateMaterials()
{
    auto blackPlatform = std::make_unique<AssetMaterial>();
    blackPlatform->Name = "blackPlatform";
    blackPlatform->BufferIndex = 0;
    blackPlatform->DiffuseSlot = 5;
    blackPlatform->NormalSlot = 5;
    blackPlatform->BaseColor = XMFLOAT4(0.05f, 0.05f, 0.05f, 1.0f);
    blackPlatform->Reflectance = XMFLOAT3(0.01f, 0.01f, 0.01f);
    blackPlatform->Smoothness = 0.8f;

    auto blue = std::make_unique<AssetMaterial>();
    blue->Name = "blue";
    blue->BufferIndex = 1;
    blue->DiffuseSlot = 5;
    blue->NormalSlot = 5;
    blue->BaseColor = XMFLOAT4(0.2f, 0.4f, 0.9f, 1.0f);
    blue->Reflectance = XMFLOAT3(0.1f, 0.1f, 0.1f);
    blue->Smoothness = 0.3f;

    mMaterialMap["blackPlatform"] = std::move(blackPlatform);
    mMaterialMap["blue"] = std::move(blue);
}

void TemporalDemo::PopulateScene()
{
    auto floor = std::make_unique<SceneObject>();
    floor->Transform = MathHelper::Identity4x4();
    floor->PreviousTransform = MathHelper::Identity4x4();
    floor->ConstantIndex = 0;
    floor->Material = mMaterialMap["blackPlatform"].get();
    floor->Geometry = mGeometryMap["scene"].get();
    floor->Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    floor->IndexCount = floor->Geometry->DrawArgs["grid"].IndexCount;
    floor->IndexStart = floor->Geometry->DrawArgs["grid"].StartIndexLocation;
    floor->VertexBase = floor->Geometry->DrawArgs["grid"].BaseVertexLocation;
    mActivePass[(int)RenderPass::Main].push_back(floor.get());
    mAllObjects.push_back(std::move(floor));

    UINT cbIndex = 1;

    auto sphere = std::make_unique<SceneObject>();
    XMMATRIX sphereWorld = XMMatrixTranslation(0.0f, 3.5f, 0.0f);
    XMStoreFloat4x4(&sphere->Transform, sphereWorld);
    XMStoreFloat4x4(&sphere->PreviousTransform, sphereWorld);
    sphere->ConstantIndex = cbIndex++;
    sphere->Material = mMaterialMap["blue"].get();
    sphere->Geometry = mGeometryMap["scene"].get();
    sphere->Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    sphere->IndexCount = sphere->Geometry->DrawArgs["sphere"].IndexCount;
    sphere->IndexStart = sphere->Geometry->DrawArgs["sphere"].StartIndexLocation;
    sphere->VertexBase = sphere->Geometry->DrawArgs["sphere"].BaseVertexLocation;
    mActivePass[(int)RenderPass::Main].push_back(sphere.get());
    mAllObjects.push_back(std::move(sphere));
}

void TemporalDemo::RenderItems(ID3D12GraphicsCommandList* list, const std::vector<SceneObject*>& items)
{
    UINT objSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectBuffer = mCurrentFrame->ObjectCB->Resource();
    auto materialBuffer = mCurrentFrame->MaterialBuffer->Resource();

    list->SetGraphicsRootShaderResourceView(3, materialBuffer->GetGPUVirtualAddress());

    for (size_t i = 0; i < items.size(); ++i)
    {
        auto obj = items[i];

        list->IASetVertexBuffers(0, 1, &obj->Geometry->VertexBufferView());
        list->IASetIndexBuffer(&obj->Geometry->IndexBufferView());
        list->IASetPrimitiveTopology(obj->Topology);

        D3D12_GPU_VIRTUAL_ADDRESS objAddress = objectBuffer->GetGPUVirtualAddress() + obj->ConstantIndex * objSize;

        list->SetGraphicsRootConstantBufferView(0, objAddress);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mShaderHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(obj->Material->DiffuseSlot, mCbvSrvUavDescriptorSize);
        list->SetGraphicsRootDescriptorTable(2, tex);

        list->DrawIndexedInstanced(obj->IndexCount, 1, obj->IndexStart, obj->VertexBase, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> TemporalDemo::GetStaticSamplers()
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

    const CD3DX12_STATIC_SAMPLER_DESC anisoWrap(
        4,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        8);

    const CD3DX12_STATIC_SAMPLER_DESC anisoClamp(
        5,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f,
        8);

    const CD3DX12_STATIC_SAMPLER_DESC shadow(
        6,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f,
        16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisoWrap, anisoClamp,
        shadow
    };
}