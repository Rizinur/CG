#include "FrameResource.h"

RenderFrame::RenderFrame(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    FrameBuffer = std::make_unique<UploadBuffer<FrameConstants>>(device, passCount, true);
    ObjectBuffer = std::make_unique<UploadBuffer<PerObjectData>>(device, objectCount, true);
    SkyBuffer = std::make_unique<UploadBuffer<SkyAtmosphereData>>(device, 1, true);
    SurfaceBuffer = std::make_unique<UploadBuffer<SurfaceProperties>>(device, materialCount, false);
}

RenderFrame::~RenderFrame()
{
}