#include "InventorySwapchain.h"
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace preyvr::dll;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
namespace {
struct Runtime {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> image;
    XrSwapchainCreateInfo created{};
    std::vector<std::string> calls;
    std::vector<int64_t> formats{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    XrResult acquireResult=XR_SUCCESS,waitResult=XR_SUCCESS,releaseResult=XR_SUCCESS;
    uint32_t index=0;
    bool nullImage=false;
    unsigned serial=0;
    XrSwapchain handle=XR_NULL_HANDLE;
} r;
XrSession session=reinterpret_cast<XrSession>(0x1000);
D3D11_TEXTURE2D_DESC Description(unsigned width=640) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width=width;d.Height=480;d.ArraySize=1;d.MipLevels=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
    d.BindFlags=D3D11_BIND_RENDER_TARGET;return d;
}
ComPtr<ID3D11Texture2D> Texture(const D3D11_TEXTURE2D_DESC& d,uint32_t pixel) {
    std::vector<uint32_t> pixels(static_cast<size_t>(d.Width)*d.Height,pixel);
    D3D11_SUBRESOURCE_DATA data{pixels.data(),d.Width*4,0};
    ComPtr<ID3D11Texture2D> t;CHECK(SUCCEEDED(r.device->CreateTexture2D(&d,&data,&t)));return t;
}
void CheckPixels(ID3D11Texture2D* texture,uint32_t pixel) {
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(r.device->CreateTexture2D(&d,nullptr,&staging)));
    r.context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(r.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    bool matches=true;
    for(unsigned y=0;y<d.Height;++y) {
        const auto row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map.pData)+y*map.RowPitch);
        matches=matches && std::all_of(row,row+d.Width,[&](auto v){return v==pixel;});
    }
    r.context->Unmap(staging.Get(),0);CHECK(matches);
}
XrResult XRAPI_CALL Formats(XrSession s,uint32_t capacity,uint32_t* count,int64_t* out) {
    CHECK(s!=XR_NULL_HANDLE);*count=static_cast<uint32_t>(r.formats.size());
    if(capacity) {CHECK(capacity>=*count);std::copy(r.formats.begin(),r.formats.end(),out);}
    return XR_SUCCESS;
}
XrResult XRAPI_CALL Create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    r.calls.push_back("create");r.created=*info;
    auto d=Description(info->width);d.Height=info->height;d.Format=static_cast<DXGI_FORMAT>(info->format);
    r.image=Texture(d,0x12345678);
    r.handle=reinterpret_cast<XrSwapchain>(static_cast<uintptr_t>(0x2000+(++r.serial)*0x10));
    *out=r.handle;return XR_SUCCESS;
}
XrResult XRAPI_CALL Images(XrSwapchain s,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    CHECK(s==r.handle);*count=1;
    if(capacity)reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture=r.nullImage?nullptr:r.image.Get();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL Acquire(XrSwapchain s,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    CHECK(s==r.handle);r.calls.push_back("acquire");*index=r.index;return r.acquireResult;
}
XrResult XRAPI_CALL Wait(XrSwapchain s,const XrSwapchainImageWaitInfo* info) {
    CHECK(s==r.handle);CHECK(info->timeout>0 && info->timeout<=50000000);
    r.calls.push_back("wait");return r.waitResult;
}
XrResult XRAPI_CALL Release(XrSwapchain s,const XrSwapchainImageReleaseInfo*) {
    CHECK(s==r.handle);r.calls.push_back("release");return r.releaseResult;
}
XrResult XRAPI_CALL Destroy(XrSwapchain s) {
    CHECK(s==r.handle);r.calls.push_back("destroy");r.handle=XR_NULL_HANDLE;r.image.Reset();return XR_SUCCESS;
}
InventorySwapchainApi Api(){return {Formats,Create,Images,Acquire,Wait,Release,Destroy};}
void Calls(std::initializer_list<const char*> expected) {
    std::vector<std::string> e(expected.begin(),expected.end());CHECK(r.calls==e);r.calls.clear();
}
void LifecycleAndPixels() {
    InventorySwapchain owner(Api());auto d=Description();
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));auto first=owner.Handle();
    CHECK(r.created.arraySize==1 && r.created.faceCount==1 && r.created.mipCount==1 && r.created.sampleCount==1);
    CHECK(r.created.format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});
    auto source=Texture(d,0x80402010),world=Texture(d,0xFF13579B);
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::ready);
    Calls({"acquire","wait","release"});
    CheckPixels(r.image.Get(),0x80402010);CheckPixels(world.Get(),0xFF13579B);
    CHECK(owner.Prepare(session,Description(800),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));CHECK(owner.Handle()!=first);Calls({"destroy","create"});
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::failed);Calls({});
    CHECK(owner.Prepare(reinterpret_cast<XrSession>(0x1100),d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"destroy","create"});
    CHECK(owner.Copy(nullptr,source.Get())==InventorySwapchain::Upload::failed);Calls({});
    owner.Reset();CHECK(!owner.Handle());Calls({"destroy"});
    owner.Reset();Calls({});
}
void Refusals() {
    InventorySwapchain owner(Api());auto d=Description();
    d.ArraySize=2;CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    d=Description();d.SampleDesc.Count=4;CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    d=Description();d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    d=Description();r.nullImage=true;
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));CHECK(!owner.Handle());Calls({"create","destroy"});
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({}); // no allocation loop on refusal
    r.nullImage=false;owner.Reset();CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});
    auto source=Texture(d,0xCC664422);
    r.acquireResult=XR_ERROR_RUNTIME_FAILURE;
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::failed);Calls({"acquire"});
    CheckPixels(r.image.Get(),0x12345678);r.acquireResult=XR_SUCCESS;
    r.index=2;
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::failed);
    Calls({"acquire","wait","release"});CheckPixels(r.image.Get(),0x12345678);r.index=0;
    // A positive timeout is not a waited image: no copy, no release, no retry.
    r.waitResult=XR_TIMEOUT_EXPIRED;
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::sessionFault);
    Calls({"acquire","wait"});CheckPixels(r.image.Get(),0x12345678);
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::sessionFault);Calls({});
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    owner.Reset();Calls({"destroy"});r.waitResult=XR_SUCCESS;
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});r.releaseResult=XR_ERROR_RUNTIME_FAILURE;
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::sessionFault);
    Calls({"acquire","wait","release"});
    CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::sessionFault);Calls({});
    r.releaseResult=XR_SUCCESS;
}
void PresentationPolicy() {
    // Source resource spelling and runtime preference order do not decide the
    // copied image's interpretation: the active main/menu format does.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM,
               DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM};
    InventorySwapchain owner(Api());
    for(const auto sourceFormat:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}) {
        auto d=Description();d.Format=sourceFormat;
        const auto srgb=sourceFormat==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        CHECK(owner.Prepare(session,d,sourceFormat));CHECK(r.created.format==sourceFormat);Calls({"create"});
        auto source=Texture(d,0x80402010);
        CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::ready);
        CheckPixels(r.image.Get(),0x80402010);Calls({"acquire","wait","release"});
        CHECK(owner.Prepare(session,d,srgb));CHECK(r.created.format==srgb);Calls({"destroy","create"});
        CHECK(owner.Prepare(session,d,srgb));Calls({});
        CHECK(owner.Copy(r.context.Get(),source.Get())==InventorySwapchain::Upload::ready);
        CheckPixels(r.image.Get(),0x80402010);Calls({"acquire","wait","release"});
        owner.Reset();Calls({"destroy"});
    }
    // Do not silently change transfer function if the requested one is absent.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    CHECK(!owner.Prepare(session,Description(),DXGI_FORMAT_R8G8B8A8_UNORM));Calls({});
    CHECK(!owner.Prepare(session,Description(),DXGI_FORMAT_R8G8B8A8_TYPELESS));Calls({});
    CHECK(!owner.Prepare(session,Description(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB));Calls({});
}
}
int main() {
    try {
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&r.device,nullptr,&r.context)));
        LifecycleAndPixels();Refusals();Calls({"destroy"});
        PresentationPolicy();
        std::cout<<"inventory_swapchain PASS: real WARP copy/alpha, independent world texture, lifetime, resize, inherited linear/sRGB format, XR failures\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
