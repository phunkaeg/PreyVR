#include "HudLayer.h"
#include "Bootstrap.h"
#include "MinHookInit.h"
#include "Logger.h"
#include "HudBridge.h"
#include "XrSessionHost.h"
#include "InventorySwapchain.h"
#include "preyvr/EngineMap.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/UiPanel.h"
#include "preyvr/InventoryDepth.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>

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
// Opt-in pending headset acceptance. Redirection consumes the movie's native
// draw, so it also requires a live, compatible swapchain consumer lease.
std::atomic<bool> inventoryCapture{false};
struct InventoryConsumer {D3D11_TEXTURE2D_DESC description{};std::uint64_t stamp=0;
    float panelWidth=0,eyeSeparation=0;bool stereo=false;};
LatestSnapshot<InventoryConsumer> inventoryConsumer;
std::atomic<unsigned long long> refused{0};

// **Generalised by purpose, not by widening a name check.**
//
// This captured only DanielleHUD, and only while gameplay input was allowed --
// which is precisely when the inventory is NOT up, so the inventory could never
// be extracted (INVENTORY-HOLOGRAM-READINESS-2026-09-11, "Already available").
// The two movies want opposite gates and must not share a texture, so identity,
// target lifetime and capture freshness are kept separate per movie rather than
// one being made to stand in for the other.
enum class Movie:unsigned {hud=0,pda=1};
constexpr unsigned kMovies=2;
constexpr const char* kMovieNames[kMovies]={"DanielleHUD","DaniellePDA"};

struct Identity {std::uintptr_t proxy=0;std::uint64_t stamp=0;};
struct Presentation {float width=0,height=0,distance=2;bool active=false;std::uint64_t stamp=0;};
LatestSnapshot<Identity> seen[kMovies];
LatestSnapshot<Presentation> presentation;
std::atomic<unsigned long long> captured[kMovies]{};
std::atomic<unsigned long long> identified[kMovies]{};
// **The observed proxy vtable when it did not match**, kept because it is the
// one value that turns a silent "the inventory never captures" into a fix.
// RE-INVENTORY-DEPTH-2026-09-11 confirmed the PDA shares the element vtable
// 0x1CAB358 and player vtable 0x1DB56D8 with the HUD, but nothing has yet
// proved its render proxy at player+8 carries 0x1DB58B8 as the HUD's does. If
// it does not, this reports the real RVA instead of leaving us to guess.
std::atomic<std::uintptr_t> proxyMismatch[kMovies]{};

struct Capture {
    ComPtr<ID3D11Texture2D> texture,depth;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depthView;
    std::uint64_t stamp=0;
    DWORD thread=0;
};
Capture captures[kMovies];
Capture inventoryRight;
std::atomic<bool> inventoryStereo{false};
std::atomic<unsigned> inventoryDepthPercent{25};
std::atomic<unsigned long long> stereoPairs{0},stereoMatrices{0},stereoRefused{0};
using MovieDisplay=void(__fastcall*)(void*);
using CalcMatrix=void(__fastcall*)(void*,const void*,float*);
MovieDisplay originalDisplay=nullptr;
CalcMatrix originalCalc=nullptr;
bool stereoInstalled=false;
thread_local void* stereoRoot=nullptr;
thread_local int stereoEye=-1;
thread_local bool stereoProduced=false;
thread_local InventoryConsumer stereoConsumer{};
thread_local float stereoScale=0;

// The redirection is scoped to the native callback on this thread. Offscreen
// Scaleform filter targets stay native; only its original destination changes.
thread_local ID3D11RenderTargetView* originalDestination=nullptr;
thread_local ID3D11DepthStencilView* originalDepth=nullptr;
thread_local ID3D11DeviceContext* activeContext=nullptr;
// Which capture the in-flight callback is writing into. Thread-local with the
// rest: two movies never render on top of each other, but the substitute must
// follow the movie rather than a file-scope single target.
thread_local ID3D11RenderTargetView* activeTarget=nullptr;
thread_local ID3D11DepthStencilView* activeDepthView=nullptr;

void __fastcall StereoMatrix(void* renderer,const void* source,float* output) {
    originalCalc(renderer,source,output);
    if(stereoEye<0 || !activeContext || !output)return;
    // CalcTransMat3D on the supported Steam image consumes view at +0x1F8.
    // The recorded view has m22=-1 and translation Z=-36882.855; retain the
    // native viewpoint and use its actual homogeneous reference depth.
    const auto view=reinterpret_cast<const float*>(static_cast<const char*>(renderer)+0x1F8);
    if(std::abs(view[10]+1.f)>.001f || std::abs(view[11])>.001f ||
       std::abs(view[15]-1.f)>.001f || !std::isfinite(view[14]) || view[14]>=-1.f) {
        stereoRefused.fetch_add(1,std::memory_order_relaxed);return;
    }
    std::array<float,16> matrix;std::memcpy(matrix.data(),output,sizeof(matrix));
    const float eye=(stereoEye==0?-.5f:.5f)*stereoConsumer.eyeSeparation;
    if(inventory::ApplyStereoParallax(matrix,-view[14],stereoConsumer.panelWidth,eye,stereoScale)) {
        std::memcpy(output,matrix.data(),sizeof(matrix));
        stereoMatrices.fetch_add(1,std::memory_order_relaxed);
    } else stereoRefused.fetch_add(1,std::memory_order_relaxed);
}
void __fastcall StereoDisplay(void* root) {
    if(root!=stereoRoot || !activeContext || stereoEye>=0) {originalDisplay(root);return;}
    // Entered from the ONE native Flash callback, inside both native locks.
    // Each Display builds/destroys its own draw context. No Advance, input,
    // outer callback replay, or extra release of the player occurs here.
    for(int eye=0;eye<2;++eye) {
        auto& target=eye==0?captures[1]:inventoryRight;
        if(originalDepth) {
            ComPtr<ID3D11Resource> source;originalDepth->GetResource(&source);
            activeContext->CopyResource(target.depth.Get(),source.Get());
        }
        const float clear[4]{};activeContext->ClearRenderTargetView(target.target.Get(),clear);
        activeTarget=target.target.Get();activeDepthView=target.depthView.Get();
        originalTargets(activeContext,1,&activeTarget,activeDepthView);
        stereoEye=eye;originalDisplay(root);stereoEye=-1;
    }
    stereoProduced=true;
}

bool Identify(void* element,std::uintptr_t base,std::uintptr_t* proxy,Movie* movie) {
    __try {
        auto e=static_cast<std::uint8_t*>(element);
        if(!e || *reinterpret_cast<std::uintptr_t*>(e)!=base+0x1CAB358)return false;
        const char* name=*reinterpret_cast<const char**>(e+0x30);
        if(!name)return false;
        unsigned which=kMovies;
        for(unsigned i=0;i<kMovies;++i)if(std::strcmp(name,kMovieNames[i])==0){which=i;break;}
        if(which==kMovies)return false;
        auto p=*reinterpret_cast<std::uintptr_t*>(e+0x58);
        if(!p || *reinterpret_cast<std::uintptr_t*>(p)!=base+0x1DB56D8)return false;
        const auto proxyVtable=*reinterpret_cast<std::uintptr_t*>(p+8);
        if(proxyVtable!=base+0x1DB58B8) {
            // Fail closed, but say what was actually there. Recorded as an RVA
            // so it can be read straight against the module.
            proxyMismatch[which].store(proxyVtable>base?proxyVtable-base:proxyVtable,
                                       std::memory_order_relaxed);
            return false;
        }
        *proxy=p+8;*movie=static_cast<Movie>(which);
        identified[which].fetch_add(1,std::memory_order_relaxed);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void __fastcall ObserveElement(void* element) {
    std::uintptr_t proxy=0;Movie movie=Movie::hud;
    if(Identify(element,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll")),&proxy,&movie))
        seen[static_cast<unsigned>(movie)].Publish({proxy,MonotonicNanoseconds()});
    originalElement(element);
}
void STDMETHODCALLTYPE RedirectTargets(ID3D11DeviceContext* context,UINT count,
        ID3D11RenderTargetView*const* targets,ID3D11DepthStencilView* ds) {
    if(context==activeContext && count==1 && targets && targets[0]==originalDestination && activeTarget) {
        auto substitute=activeTarget;
        originalTargets(context,1,&substitute,ds==originalDepth?activeDepthView:ds);
    } else originalTargets(context,count,targets,ds);
}
bool EnsureTexture(ID3D11Device* device,ID3D11RenderTargetView* original,
                   ID3D11DepthStencilView* ds,Capture& capture) {
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
    D3D11_TEXTURE2D_DESC old{};if(capture.texture)capture.texture->GetDesc(&old);
    if(!capture.texture || old.Width!=d.Width || old.Height!=d.Height || old.Format!=view.Format) {
        capture.texture.Reset();capture.target.Reset();capture.depth.Reset();capture.depthView.Reset();
        d.Format=view.Format;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&capture.texture)) ||
           FAILED(device->CreateRenderTargetView(capture.texture.Get(),&view,&capture.target)))return false;
    }
    // Preserve the native stencil contents used to clip Flash into its viewport.
    if(ds) {
        ComPtr<ID3D11Resource> dsResource;ds->GetResource(&dsResource);
        ComPtr<ID3D11Texture2D> dsTexture;if(FAILED(dsResource.As(&dsTexture)))return false;
        D3D11_TEXTURE2D_DESC dd{},previous{};dsTexture->GetDesc(&dd);if(capture.depth)capture.depth->GetDesc(&previous);
        if(dd.Width!=d.Width || dd.Height!=d.Height || dd.SampleDesc.Count!=1)return false;
        if(!capture.depth || previous.Width!=dd.Width || previous.Height!=dd.Height || previous.Format!=dd.Format) {
            capture.depth.Reset();capture.depthView.Reset();
            D3D11_DEPTH_STENCIL_VIEW_DESC vd{};ds->GetDesc(&vd);
            if(FAILED(device->CreateTexture2D(&dd,nullptr,&capture.depth)) ||
               FAILED(device->CreateDepthStencilView(capture.depth.Get(),&vd,&capture.depthView)))return false;
        }
    } else {capture.depth.Reset();capture.depthView.Reset();}
    return true;
}
// **The gate differs by movie, and that is the whole point.** The gameplay HUD
// is only meaningful while gameplay input is allowed; the PDA is only on screen
// while it is not. Sharing one gate is what kept the inventory uncapturable.
bool WantMovie(Movie movie) {
    switch(movie) {
        case Movie::hud: return enabled.load() && HudGameplayInputAllowed();
        case Movie::pda: return inventoryCapture.load(std::memory_order_acquire) &&
                                HudInventoryIsOpen();
    }
    return false;
}
void* InventoryRoot(void* proxy,std::uintptr_t base) {
    __try {
        auto root=*reinterpret_cast<void**>(static_cast<char*>(proxy)+0xA8);
        return root && *reinterpret_cast<std::uintptr_t*>(root)==base+0x1EB4B60?root:nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
void __fastcall CaptureFlash(void* proxy,bool release) {
    const auto address=reinterpret_cast<std::uintptr_t>(proxy);
    const auto now=MonotonicNanoseconds();
    unsigned which=kMovies;
    if((enabled.load() || inventoryCapture.load()) && XrSessionStatusValue()==1 && !activeContext) {
        for(unsigned i=0;i<kMovies;++i) {
            Identity id{};
            if(seen[i].TryRead(id) && id.proxy==address && FreshSample(now,id.stamp)) {
                captures[i].stamp=0; // This callback supersedes any older capture, including on refusal.
                if(i==1)inventoryRight.stamp=0;
                if(WantMovie(static_cast<Movie>(i))) {which=i;break;}
            }
        }
    }
    if(which==kMovies) {originalFlash(proxy,release);return;}
    InventoryConsumer consumer{};
    if(which==static_cast<unsigned>(Movie::pda) &&
       (!inventoryConsumer.TryRead(consumer) || !FreshSample(now,consumer.stamp))) {
        originalFlash(proxy,release);return;
    }
    Capture& capture=captures[which];
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    auto renderer=*reinterpret_cast<std::uintptr_t*>(base+engine::RendererLayout::singletonPointerRva);
    auto device=*reinterpret_cast<ID3D11Device**>(renderer+engine::RendererLayout::device);
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    std::array<ID3D11RenderTargetView*,8> saved{};ID3D11DepthStencilView* ds=nullptr;
    context->OMGetRenderTargets(8,saved.data(),&ds);
    bool single=saved[0]!=nullptr;for(unsigned i=1;i<8;++i)single=single && !saved[i];
    bool ready=single && EnsureTexture(device,saved[0],ds,capture);
    if(ready && which==static_cast<unsigned>(Movie::pda)) {
        D3D11_TEXTURE2D_DESC d{};capture.texture->GetDesc(&d);
        ready=InventoryTextureCompatible(consumer.description,d);
    }
    stereoProduced=false;
    if(ready && which==1 && inventoryStereo.load() && consumer.stereo &&
       consumer.panelWidth>=.1f && consumer.eyeSeparation>=.04f && consumer.eyeSeparation<=.09f &&
       EnsureTexture(device,saved[0],ds,inventoryRight)) {
        stereoRoot=InventoryRoot(proxy,base);
        stereoConsumer=consumer;
        stereoScale=inventoryDepthPercent.load()*.01f;
    }
    if(ready) {
        if(ds) {ComPtr<ID3D11Resource> source;ds->GetResource(&source);context->CopyResource(capture.depth.Get(),source.Get());}
        const float clear[4]={0,0,0,0};context->ClearRenderTargetView(capture.target.Get(),clear);
        originalDestination=saved[0];originalDepth=ds;activeContext=context.Get();
        activeTarget=capture.target.Get();activeDepthView=capture.depthView.Get();
        auto rt=capture.target.Get();originalTargets(context.Get(),1,&rt,capture.depthView.Get());
    } else refused.fetch_add(1);
    // Exactly one outer call. The opt-in Display hook draws inside its locks.
    originalFlash(proxy,release);
    stereoRoot=nullptr;
    if(ready) {
        activeContext=nullptr;originalDestination=nullptr;originalDepth=nullptr;
        activeTarget=nullptr;activeDepthView=nullptr;
        originalTargets(context.Get(),8,saved.data(),ds);
        capture.stamp=MonotonicNanoseconds();capture.thread=GetCurrentThreadId();
        captured[which].fetch_add(1);
        if(which==1 && stereoProduced) {
            inventoryRight.stamp=capture.stamp;inventoryRight.thread=capture.thread;
            stereoPairs.fetch_add(1);
        }
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
bool InstallStereo() {
    if(stereoInstalled)return true;
    if(SmokeStatus()!=2)return false; // Complete supported-module gate, not only a DLL pin.
    if(!Install())return false;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    constexpr unsigned char displayBytes[]={0x40,0x55,0x56,0x48,0x81,0xEC,0x68,0x03,0,0};
    constexpr unsigned char calcBytes[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x48,0x8D,0xA8,0x48,0xFF,0xFF,0xFF};
    const auto display=reinterpret_cast<void*>(base+0x18AB710);
    const auto calc=reinterpret_cast<void*>(base+0xDB9620);
    if(std::memcmp(display,displayBytes,sizeof(displayBytes)) ||
       std::memcmp(calc,calcBytes,sizeof(calcBytes)))return false;
    if(MH_CreateHook(display,reinterpret_cast<void*>(StereoDisplay),reinterpret_cast<void**>(&originalDisplay))!=MH_OK)return false;
    if(MH_CreateHook(calc,reinterpret_cast<void*>(StereoMatrix),reinterpret_cast<void**>(&originalCalc))!=MH_OK) {
        MH_RemoveHook(display);return false;
    }
    if(MH_EnableHook(calc)!=MH_OK) {MH_RemoveHook(calc);MH_RemoveHook(display);return false;}
    if(MH_EnableHook(display)!=MH_OK) {MH_DisableHook(calc);MH_RemoveHook(calc);MH_RemoveHook(display);return false;}
    stereoInstalled=true;return true;
}
ID3D11Texture2D* BorrowTexture(Movie movie) {
    Capture& capture=captures[static_cast<unsigned>(movie)];
    return capture.thread==GetCurrentThreadId() && FreshSample(MonotonicNanoseconds(),capture.stamp)
        ? capture.texture.Get() : nullptr;
}
}
DWORD SetHudLayerEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !Install())return 2;
    enabled.store(value!=0);return 0;
}
bool HudLayerEnabled(){return enabled.load();}
DWORD SetInventoryCaptureEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !Install())return 2;
    if(!value)inventoryConsumer.Clear();
    inventoryCapture.store(value!=0,std::memory_order_release);
    lifecycle::Log(std::string("preyvr_hud_layer inventory_capture=")+(value?"1":"0")+
                   (value?" note=waiting_for_inventory_swapchain_consumer":""));
    return 0;
}
bool InventoryCaptureEnabled(){return inventoryCapture.load(std::memory_order_acquire);}
void RefuseInventoryCapture(const char* reason){
    inventoryConsumer.Clear();
    if(inventoryCapture.exchange(false))lifecycle::Log(std::string("preyvr_hud_layer inventory_disabled reason=")+reason);
}
void RefuseHudLayer(const char* reason){
    if(enabled.exchange(false))lifecycle::Log(std::string("preyvr_hud_layer disabled reason=")+reason);
}
std::string HudLayerReport(){
    // Per movie, because "captured=0" on its own never said which movie was
    // missing, nor whether identification or the gate was the reason.
    InventoryConsumer consumer{};
    const bool consumerReady=inventoryConsumer.TryRead(consumer) && FreshSample(MonotonicNanoseconds(),consumer.stamp);
    std::string out="hudLayer="+std::to_string(enabled.load())+
        " inventoryCapture="+std::to_string(inventoryCapture.load())+
        " inventoryConsumer="+std::to_string(consumerReady)+
        " inventoryLayerFrames="+std::to_string(InventoryLayerFrameCount())+
        " refused="+std::to_string(refused.load());
    for(unsigned i=0;i<kMovies;++i) {
        out+=std::string(" ")+kMovieNames[i]+"={identified="+std::to_string(identified[i].load())+
             " captured="+std::to_string(captured[i].load());
        const auto mismatch=proxyMismatch[i].load();
        if(mismatch)out+=" proxyVtableRva=0x"+[&]{char b[32];std::snprintf(b,sizeof(b),"%llX",
            static_cast<unsigned long long>(mismatch));return std::string(b);}();
        out+="}";
    }
    return out;
}
ID3D11Texture2D* HudLayerTexture(){
    auto texture=BorrowTexture(Movie::hud);
    captures[static_cast<unsigned>(Movie::hud)].stamp=0;
    return texture;
}
ID3D11Texture2D* InventoryLayerTexture(ID3D11Texture2D** rightEye){
    auto texture=BorrowTexture(Movie::pda);
    if(rightEye)*rightEye=texture && inventoryRight.stamp==captures[1].stamp &&
        inventoryRight.thread==GetCurrentThreadId()?inventoryRight.texture.Get():nullptr;
    // The host drains this at every render-frame boundary, including skips.
    captures[static_cast<unsigned>(Movie::pda)].stamp=0;
    inventoryRight.stamp=0;
    return texture;
}
bool PeekInventoryPair(ID3D11Texture2D** left,ID3D11Texture2D** right) {
    *left=BorrowTexture(Movie::pda);
    *right=*left && inventoryRight.stamp==captures[1].stamp &&
        inventoryRight.thread==GetCurrentThreadId()?inventoryRight.texture.Get():nullptr;
    return *left && *right;
}
void SetInventoryConsumerReady(const D3D11_TEXTURE2D_DESC* description,float width,float separation,bool stereo){
    if(description)inventoryConsumer.Publish({*description,MonotonicNanoseconds(),width,separation,stereo});
    else inventoryConsumer.Clear();
}
DWORD SetInventoryStereoEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !InstallStereo())return 2;
    inventoryStereo.store(value!=0);return 0;
}
bool InventoryStereoEnabled(){return inventoryStereo.load();}
DWORD SetInventoryDepthPercent(unsigned value) {
    if(value>100)return 1;
    inventoryDepthPercent.store(value);return 0;
}
std::string InventoryStereoReport() {
    return "enabled="+std::to_string(inventoryStereo.load())+" depthPercent="+std::to_string(inventoryDepthPercent.load())+
        " capturedPairs="+std::to_string(stereoPairs.load())+" matrices="+std::to_string(stereoMatrices.load())+
        " refusedMatrices="+std::to_string(stereoRefused.load());
}
void SetHudLayerPresentation(bool active,float width,float height,float distance){presentation.Publish({width,height,distance,active,MonotonicNanoseconds()});}
bool HudLayerReticle(float tanX,float tanY,float& x,float& y){
    Presentation p{};if(!enabled.load() || !presentation.TryRead(p) || !p.active ||
        !FreshSample(MonotonicNanoseconds(),p.stamp) || p.width<=0 || p.height<=0)return false;
    const auto fraction=ui::PanelFraction(tanX,tanY,p.width,p.height,p.distance);
    if(!fraction)return false;x=(*fraction)[0];y=(*fraction)[1];return true;
}
}
