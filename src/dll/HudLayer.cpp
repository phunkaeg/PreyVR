#include "HudLayer.h"
#include "Bootstrap.h"
#include "MinHookInit.h"
#include "Logger.h"
#include "HudBridge.h"
#include "XrSessionHost.h"
#include "preyvr/EngineMap.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/UiPanel.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>
#include <mutex>

namespace preyvr::dll {
namespace {
using Microsoft::WRL::ComPtr;
using ElementRender=void(__fastcall*)(void*);
using FlashRender=void(__fastcall*)(void*,bool);
using SetTargets=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*);
ElementRender originalElement=nullptr;
FlashRender originalFlash=nullptr;
SetTargets originalTargets=nullptr;
std::mutex installMutex;
bool installed=false;
std::atomic<bool> enabled{false};
std::atomic<unsigned long long> captured{0},refused{0};
struct Identity {std::uintptr_t proxy=0;std::uint64_t stamp=0;};
struct Presentation {float width=0,height=0,distance=2;bool active=false;std::uint64_t stamp=0;};
LatestSnapshot<Identity> hud;
LatestSnapshot<Presentation> presentation;
ComPtr<ID3D11Texture2D> texture,depth;
ComPtr<ID3D11RenderTargetView> target;
ComPtr<ID3D11DepthStencilView> depthView;
std::uint64_t textureStamp=0;
DWORD renderThread=0;
// The redirection is scoped to the native callback on this thread. Offscreen
// Scaleform filter targets stay native; only its original destination changes.
thread_local ID3D11RenderTargetView* originalDestination=nullptr;
thread_local ID3D11DepthStencilView* originalDepth=nullptr;
thread_local ID3D11DeviceContext* activeContext=nullptr;

bool Identify(void* element,std::uintptr_t base,std::uintptr_t* proxy) {
    __try {
        auto e=static_cast<std::uint8_t*>(element);
        if(!e || *reinterpret_cast<std::uintptr_t*>(e)!=base+0x1CAB358 ||
           std::strcmp(*reinterpret_cast<const char**>(e+0x30),"DanielleHUD")!=0)return false;
        auto p=*reinterpret_cast<std::uintptr_t*>(e+0x58);
        if(!p || *reinterpret_cast<std::uintptr_t*>(p)!=base+0x1DB56D8 ||
           *reinterpret_cast<std::uintptr_t*>(p+8)!=base+0x1DB58B8)return false;
        *proxy=p+8;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void __fastcall ObserveElement(void* element) {
    std::uintptr_t proxy=0;
    if(Identify(element,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll")),&proxy))
        hud.Publish({proxy,MonotonicNanoseconds()});
    originalElement(element);
}
void STDMETHODCALLTYPE RedirectTargets(ID3D11DeviceContext* context,UINT count,
        ID3D11RenderTargetView*const* targets,ID3D11DepthStencilView* ds) {
    if(context==activeContext && count==1 && targets && targets[0]==originalDestination) {
        auto substitute=target.Get();
        originalTargets(context,1,&substitute,ds==originalDepth?depthView.Get():ds);
    } else originalTargets(context,count,targets,ds);
}
bool EnsureTexture(ID3D11Device* device,ID3D11RenderTargetView* original,ID3D11DepthStencilView* ds) {
    ComPtr<ID3D11Resource> resource;
    original->GetResource(&resource);
    ComPtr<ID3D11Texture2D> source;
    if(FAILED(resource.As(&source)))return false;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    D3D11_RENDER_TARGET_VIEW_DESC view{};original->GetDesc(&view);
    if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || d.MipLevels!=1 ||
       d.Width<640 || d.Height<480 || d.Width>8192 || d.Height>8192)return false;
    if(view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
       view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return false;
    D3D11_TEXTURE2D_DESC old{};if(texture)texture->GetDesc(&old);
    if(!texture || old.Width!=d.Width || old.Height!=d.Height || old.Format!=view.Format) {
        texture.Reset();target.Reset();depth.Reset();depthView.Reset();
        d.Format=view.Format;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&texture)) ||
           FAILED(device->CreateRenderTargetView(texture.Get(),&view,&target)))return false;
    }
    // Preserve the native stencil contents used to clip Flash into its viewport.
    if(ds) {
        ComPtr<ID3D11Resource> dsResource;ds->GetResource(&dsResource);
        ComPtr<ID3D11Texture2D> dsTexture;if(FAILED(dsResource.As(&dsTexture)))return false;
        D3D11_TEXTURE2D_DESC dd{},previous{};dsTexture->GetDesc(&dd);if(depth)depth->GetDesc(&previous);
        if(dd.Width!=d.Width || dd.Height!=d.Height || dd.SampleDesc.Count!=1)return false;
        if(!depth || previous.Width!=dd.Width || previous.Height!=dd.Height || previous.Format!=dd.Format) {
            depth.Reset();depthView.Reset();
            D3D11_DEPTH_STENCIL_VIEW_DESC vd{};ds->GetDesc(&vd);
            if(FAILED(device->CreateTexture2D(&dd,nullptr,&depth)) ||
               FAILED(device->CreateDepthStencilView(depth.Get(),&vd,&depthView)))return false;
        }
    } else {depth.Reset();depthView.Reset();}
    return true;
}
void __fastcall CaptureFlash(void* proxy,bool release) {
    Identity id{};
    if(!enabled.load() || XrSessionStatusValue()!=1 || !HudGameplayInputAllowed() ||
       !hud.TryRead(id) || id.proxy!=reinterpret_cast<std::uintptr_t>(proxy) ||
       !FreshSample(MonotonicNanoseconds(),id.stamp) || activeContext) {originalFlash(proxy,release);return;}
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    auto renderer=*reinterpret_cast<std::uintptr_t*>(base+engine::RendererLayout::singletonPointerRva);
    auto device=*reinterpret_cast<ID3D11Device**>(renderer+engine::RendererLayout::device);
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    std::array<ID3D11RenderTargetView*,8> saved{};ID3D11DepthStencilView* ds=nullptr;
    context->OMGetRenderTargets(8,saved.data(),&ds);
    bool single=saved[0]!=nullptr;for(unsigned i=1;i<8;++i)single=single && !saved[i];
    const bool ready=single && EnsureTexture(device,saved[0],ds);
    if(ready) {
        if(ds) {ComPtr<ID3D11Resource> source;ds->GetResource(&source);context->CopyResource(depth.Get(),source.Get());}
        const float clear[4]={0,0,0,0};context->ClearRenderTargetView(target.Get(),clear);
        originalDestination=saved[0];originalDepth=ds;activeContext=context.Get();
        auto rt=target.Get();originalTargets(context.Get(),1,&rt,depthView.Get());
    } else refused.fetch_add(1);
    // Exactly one original call; retain its locking and reference-release path.
    originalFlash(proxy,release);
    if(ready) {
        activeContext=nullptr;originalDestination=nullptr;originalDepth=nullptr;
        originalTargets(context.Get(),8,saved.data(),ds);
        textureStamp=MonotonicNanoseconds();renderThread=GetCurrentThreadId();captured.fetch_add(1);
    }
    for(auto p:saved)if(p)p->Release();if(ds)ds->Release();
}
bool Install() {
    if(installed)return true;
    if(!ModulePinStatus() || !EnsureMinHook())return false;
    auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base)return false;
    constexpr std::uint8_t elementBytes[]={0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x83,0x79,0x58,0,0x4C,0x8B,0xF1,0x74,0x6A};
    constexpr std::uint8_t flashBytes[]={0x48,0x89,0x5C,0x24,8,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    if(std::memcmp(reinterpret_cast<void*>(base+0x2FEBC0),elementBytes,sizeof(elementBytes)) ||
       std::memcmp(reinterpret_cast<void*>(base+0xE8C5E0),flashBytes,sizeof(flashBytes)))return false;
    auto renderer=*reinterpret_cast<std::uintptr_t*>(base+engine::RendererLayout::singletonPointerRva);
    if(!renderer)return false;
    auto device=*reinterpret_cast<ID3D11Device**>(renderer+engine::RendererLayout::device);if(!device)return false;
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);if(!context)return false;
    auto om=(*reinterpret_cast<void***>(context.Get()))[33];
    const auto hook=[&](void* at,void* detour,void** out) {return MH_CreateHook(at,detour,out)==MH_OK;};
    // Install as one transaction; remove disabled hooks if any creation fails.
    void* a=reinterpret_cast<void*>(base+0x2FEBC0);void* b=reinterpret_cast<void*>(base+0xE8C5E0);
    if(!hook(a,reinterpret_cast<void*>(ObserveElement),reinterpret_cast<void**>(&originalElement)))return false;
    if(!hook(b,reinterpret_cast<void*>(CaptureFlash),reinterpret_cast<void**>(&originalFlash))) {MH_RemoveHook(a);return false;}
    if(!hook(om,reinterpret_cast<void*>(RedirectTargets),reinterpret_cast<void**>(&originalTargets))) {MH_RemoveHook(a);MH_RemoveHook(b);return false;}
    MH_QueueEnableHook(om);MH_QueueEnableHook(a);MH_QueueEnableHook(b);
    if(MH_ApplyQueued()!=MH_OK)return false;
    installed=true;return true;
}
}
DWORD SetHudLayerEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !Install())return 2;
    enabled.store(value!=0);return 0;
}
bool HudLayerEnabled(){return enabled.load();}
void RefuseHudLayer(const char* reason){
    if(enabled.exchange(false))lifecycle::Log(std::string("preyvr_hud_layer disabled reason=")+reason);
}
std::string HudLayerReport(){return "hudLayer="+std::to_string(enabled.load())+" captured="+std::to_string(captured.load())+" refused="+std::to_string(refused.load());}
ID3D11Texture2D* HudLayerTexture(){return renderThread==GetCurrentThreadId() && FreshSample(MonotonicNanoseconds(),textureStamp)?texture.Get():nullptr;}
void SetHudLayerPresentation(bool active,float width,float height,float distance){presentation.Publish({width,height,distance,active,MonotonicNanoseconds()});}
bool HudLayerReticle(float tanX,float tanY,float& x,float& y){
    Presentation p{};if(!enabled.load() || !presentation.TryRead(p) || !p.active ||
        !FreshSample(MonotonicNanoseconds(),p.stamp) || p.width<=0 || p.height<=0)return false;
    const auto fraction=ui::PanelFraction(tanX,tanY,p.width,p.height,p.distance);
    if(!fraction)return false;x=(*fraction)[0];y=(*fraction)[1];return true;
}
}
