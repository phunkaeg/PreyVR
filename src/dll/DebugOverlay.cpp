#include <d3d11.h>

#include "DebugOverlay.h"

#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "Logger.h"
#include "NativeWristTexture.h"
#include "PsiMedkit.h"
#include "ReticleFollow.h"
#include "SceneQuery.h"
#include "ShotRay.h"
#include "VrOptionsRuntime.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/BodyEquipment.h"
#include "preyvr/DebugDraw.h"
#include "preyvr/DebugOverlayScene.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <vector>

namespace preyvr::dll {
namespace {
using Microsoft::WRL::ComPtr;
namespace dd = preyvr::debugdraw;

constexpr std::uint64_t kRayAgeNs = 150000000ull;    // older rays are from a previous pose
constexpr std::uint64_t kSlotAgeNs = 250000000ull;
constexpr std::uint64_t kPoseAgeNs = 200000000ull;
constexpr std::size_t kMaxVertices = 60000;
constexpr float kPointerRange = 3.0f;               // left pointer without a hit, metres

std::atomic<unsigned> gMask{0};

struct RayRecord { dd::HandRay ray{}; std::uint64_t ns = 0, reference = 0; };
struct SlotsRecord { std::array<dd::Slot, 2> slots{}; std::uint64_t ns = 0; };
struct MedkitRecord { dd::Slot slot{}; std::uint64_t ns = 0; };
struct ForegripRecord { dd::Foregrip grip{}; std::uint64_t ns = 0, reference = 0; };
// The last shot, re-expressed in OpenXR space every gameplay frame so it stays
// fixed in the world while the player moves and turns.
struct ShotRecord { dd::ShotMark shot{}; std::uint64_t shotNs = 0, ns = 0, reference = 0; };
LatestSnapshot<RayRecord> gRight, gLeft;
LatestSnapshot<SlotsRecord> gHolsters;
LatestSnapshot<MedkitRecord> gMedkit;
LatestSnapshot<ForegripRecord> gForegrip;
LatestSnapshot<ShotRecord> gShot;
std::atomic<bool> gHipStored{false}, gChestStored{false};
// Last notice per slot: 0 hip, 1 chest, 2 medkit.
std::atomic<int> gNoticeKind[3]{0, 0, 0};
std::atomic<std::uint64_t> gNoticeNs[3]{0, 0, 0};
std::atomic<bool> gWristGate{false}, gWristVisible{false};
std::atomic<std::uint64_t> gWristNs{0};

std::atomic<unsigned long long> gDrawn{0}, gVertices{0}, gTruncated{0}, gFailures{0}, gQueries{0};
std::atomic<DWORD> gLastError{0};

// What the last drawn eye saw, for dbg.report / dbg.marks.
std::mutex gLastMutex;
dd::OverlayFrame gLastFrame;
dd::EyeView gLastEye;
int gLastEyeIndex = -1;
dd::TessellateStats gLastStats;

Vec3 EngineToXr(const GameplayPoseFrame& f, Vec3 anchor, Vec3 world)
{
    const Vec3 engine = Rotate(dd::Conjugate(stereo::YawQuaternion(f.yaw)), dd::Sub(world, anchor));
    return dd::Add(f.tracking.head.position, Rotate(dd::Conjugate(stereo::kOpenXrToEngine), engine));
}
Vec3 EngineDirToXr(const GameplayPoseFrame& f, Vec3 direction)
{
    return Rotate(dd::Conjugate(stereo::kOpenXrToEngine), Rotate(dd::Conjugate(stereo::YawQuaternion(f.yaw)), direction));
}
Vec3 XrDirToEngine(const GameplayPoseFrame& f, Vec3 direction)
{
    return Rotate(stereo::YawQuaternion(f.yaw), Rotate(stereo::kOpenXrToEngine, direction));
}
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The scene hit along a world ray, when the layer asks for hits.
bool Hit(const GameplayPoseFrame& frame, Vec3 origin, Vec3 direction, unsigned mask, float& distance)
{
    if (!(mask & dd::kHits)) { return false; }
    scene::Hit hit{};
    ++gQueries;
    if (!QueryDebugRay(frame, origin, direction, hit) || !(hit.distance >= 0)) { return false; }
    distance = hit.distance;
    return true;
}

dd::Notice ToNotice(equipment::SlotNotice n, int& slot)
{
    using N = equipment::SlotNotice;
    switch (n) {
    case N::MedkitReady: slot = 2; return dd::Notice::Ready;
    case N::MedkitUsed: slot = 2; return dd::Notice::Success;
    case N::MedkitEmpty: slot = 2; return dd::Notice::Empty;
    case N::MedkitDenied: slot = 2; return dd::Notice::Denied;
    case N::HipStored: case N::HipDraw: slot = 0; return dd::Notice::Success;
    case N::ChestStored: case N::ChestDraw: slot = 1; return dd::Notice::Success;
    case N::HipDenied: slot = 0; return dd::Notice::Denied;
    case N::ChestDenied: slot = 1; return dd::Notice::Denied;
    default: slot = -1; return dd::Notice::None;
    }
}

void ApplyNotice(dd::Slot& slot, int index, std::uint64_t now)
{
    const auto ns = gNoticeNs[index].load();
    if (!ns || now < ns) { return; }
    slot.notice = static_cast<dd::Notice>(gNoticeKind[index].load());
    slot.noticeAge = static_cast<float>(now - ns) * 1e-9f;
}

// Everything the overlay shows this frame, gathered from the latest snapshots.
dd::OverlayFrame Gather(unsigned mask)
{
    dd::OverlayFrame f{};
    const auto now = MonotonicNanoseconds();
    const auto reference = HeadTrackingReferenceGeneration();
    TrackingFrame t{};
    const bool tracking = TryGetTrackingFrame(t) && FreshSample(now, t.publishedNs, kPoseAgeNs);
    f.headValid = tracking && IsPoseUsable(t.head, t.headValidity, kPoseAgeNs);
    f.head = t.head;
    for (int h = 0; h < 2; ++h) {
        const auto& c = t.hands[h];
        f.hands[h].valid = tracking && IsPoseUsable(c.gripPose, c.gripValidity, kPoseAgeNs) &&
                           IsPoseUsable(c.aimPose, c.aimValidity, kPoseAgeNs);
        f.hands[h].grip = c.gripPose;
        f.hands[h].aim = c.aimPose;
        f.hands[h].gripPressed = c.gripPressed;
        f.hands[h].triggerPressed = c.triggerPressed;
    }
    // In a menu, a death screen or a panel the rays describe nothing the player
    // can act on, and they draw across the menu: only the status panel stays.
    const bool gameplay = HudGameplayInputAllowed();
    RayRecord ray{};
    if (gameplay && gRight.TryRead(ray) && FreshSample(now, ray.ns, kRayAgeNs) && ray.reference == reference) {
        f.rays[1] = ray.ray;
    }
    if (gameplay && gLeft.TryRead(ray) && FreshSample(now, ray.ns, kRayAgeNs) && ray.reference == reference) {
        f.rays[0] = ray.ray;
    }
    f.holstersEnabled = HolstersEnabled();
    SlotsRecord slots{};
    if (f.holstersEnabled && gHolsters.TryRead(slots) && FreshSample(now, slots.ns, kSlotAgeNs)) { f.holsters = slots.slots; }
    f.holsters[0].name = "HIP R";
    f.holsters[1].name = "CHEST L";
    f.holsters[0].stored = gHipStored.load();
    f.holsters[1].stored = gChestStored.load();
    ApplyNotice(f.holsters[0], 0, now);
    ApplyNotice(f.holsters[1], 1, now);
    f.medkitEnabled = MedkitSlotEnabled() != 0;
    MedkitRecord medkit{};
    if (f.medkitEnabled && gMedkit.TryRead(medkit) && FreshSample(now, medkit.ns, kSlotAgeNs)) { f.medkit = medkit.slot; }
    f.medkit.name = "MEDKIT";
    ApplyNotice(f.medkit, 2, now);
    // The wrist card is recomputed here from the same pure functions the
    // layer uses; whether the layer decided to show it comes from the layer.
    f.wrist.enabled = WristDisplayEnabled();
    if (f.wrist.enabled && f.headValid && f.hands[0].valid) {
        const bool fresh = FreshSample(now, gWristNs.load(), kPoseAgeNs);
        f.wrist.gate = fresh && gWristGate.load();
        f.wrist.visible = fresh && gWristVisible.load();
        f.wrist.card = equipment::WristPose(t.hands[0].gripPose);
        const auto m = equipment::MeasureWrist(t.head, f.wrist.card);
        f.wrist.distance = m.distance;
        f.wrist.facing = m.facing;
        f.wrist.viewing = m.viewing;
        f.wrist.facingNeeded = equipment::WristFacingNeeded(f.wrist.visible);
        f.wrist.viewingNeeded = equipment::WristViewingNeeded(f.wrist.visible);
        f.wrist.width = .18f * static_cast<float>(WristSizePercent()) * .01f;
        f.wrist.height = f.wrist.width * static_cast<float>(NativeWristHeight) / static_cast<float>(NativeWristWidth);
    } else if (f.wrist.enabled) {
        f.wrist.card.position = {NAN, NAN, NAN};
    }
    ShotRecord shotRecord{};
    if (gameplay && gShot.TryRead(shotRecord) && FreshSample(now, shotRecord.ns, kRayAgeNs) && shotRecord.reference == reference &&
        now >= shotRecord.shotNs) {
        f.shot = shotRecord.shot;
        f.shot.age = static_cast<float>(now - shotRecord.shotNs) * 1e-9f;
    }
    ForegripRecord grip{};
    if (gForegrip.TryRead(grip) && FreshSample(now, grip.ns, kRayAgeNs) && grip.reference == reference) { f.foregrip = grip.grip; }
    f.psiMode = PsiTargetMode();
    f.sceneQueryFault = SceneQueryFaulted();
    if (mask & dd::kHits) {
        f.extra = "queries " + std::to_string(gQueries.load()) + "   scene reticle " +
                  std::string(SceneReticleEnabled() ? "on" : "off");
    }
    return f;
}

// --- D3D11 ----------------------------------------------------------------

constexpr char kShaders[] = R"(
cbuffer Frame : register(b0) { float2 viewport; float srgbTarget; float unused; };
Texture2D<float> atlas : register(t0);
SamplerState atlasSampler : register(s0);
struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; float3 p : TEXCOORD1; };
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; float3 p : TEXCOORD1; };
PSIn vs(VSIn i) {
    PSIn o;
    o.pos = float4(i.pos.x / viewport.x * 2 - 1, 1 - i.pos.y / viewport.y * 2, 0, 1);
    o.uv = i.uv; o.color = i.color; o.p = i.p;
    return o;
}
float4 ps(PSIn i) : SV_Target {
    float a = i.color.a;
    if (i.p.x < 0.5) {
        a *= saturate((1 - abs(i.uv.x)) * i.p.y);
    } else if (i.p.x < 1.5) {
        a *= atlas.Sample(atlasSampler, i.uv);
    } else {
        float d = length(i.uv) * i.p.y;
        a *= saturate(i.p.y + 0.5 - d);
        if (i.p.z > 0) a *= saturate(d - i.p.z * i.p.y + 0.5);
    }
    float3 c = i.color.rgb;
    if (srgbTarget > 0.5) c = pow(c, 2.2);
    return float4(c, a);
}
)";

// Every piece of pipeline state this draw touches, restored on scope exit.
struct SavedState {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11InputLayout> layout;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    ComPtr<ID3D11Buffer> vb; UINT stride = 0, offset = 0;
    ComPtr<ID3D11Buffer> ib; DXGI_FORMAT ibFormat{}; UINT ibOffset = 0;
    ComPtr<ID3D11VertexShader> vs; std::array<ID3D11ClassInstance*, 256> vsInst{}; UINT vsCount = 256;
    ComPtr<ID3D11PixelShader> ps; std::array<ID3D11ClassInstance*, 256> psInst{}; UINT psCount = 256;
    ComPtr<ID3D11GeometryShader> gs; std::array<ID3D11ClassInstance*, 256> gsInst{}; UINT gsCount = 256;
    ComPtr<ID3D11HullShader> hs; std::array<ID3D11ClassInstance*, 256> hsInst{}; UINT hsCount = 256;
    ComPtr<ID3D11DomainShader> ds; std::array<ID3D11ClassInstance*, 256> dsInst{}; UINT dsCount = 256;
    ComPtr<ID3D11Buffer> vsCb, psCb;
    ComPtr<ID3D11ShaderResourceView> psSrv;
    ComPtr<ID3D11SamplerState> psSampler;
    ComPtr<ID3D11RasterizerState> rs;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ComPtr<ID3D11BlendState> blend; float blendFactor[4]{}; UINT sampleMask = 0;
    ComPtr<ID3D11DepthStencilState> depth; UINT stencilRef = 0;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Predicate> predicate; BOOL predicateValue = FALSE;

    explicit SavedState(ID3D11DeviceContext* context) : c(context)
    {
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topology);
        c->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
        c->VSGetShader(&vs, vsInst.data(), &vsCount);
        c->PSGetShader(&ps, psInst.data(), &psCount);
        c->GSGetShader(&gs, gsInst.data(), &gsCount);
        c->HSGetShader(&hs, hsInst.data(), &hsCount);
        c->DSGetShader(&ds, dsInst.data(), &dsCount);
        c->VSGetConstantBuffers(0, 1, &vsCb);
        c->PSGetConstantBuffers(0, 1, &psCb);
        c->PSGetShaderResources(0, 1, &psSrv);
        c->PSGetSamplers(0, 1, &psSampler);
        c->RSGetState(&rs);
        c->RSGetViewports(&viewportCount, viewports);
        c->RSGetScissorRects(&scissorCount, scissors);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
        c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
        c->GetPredication(&predicate, &predicateValue);
    }
    ~SavedState()
    {
        c->SetPredication(predicate.Get(), predicateValue);
        c->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), dsv.Get());
        c->OMSetDepthStencilState(depth.Get(), stencilRef);
        c->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
        c->RSSetScissorRects(scissorCount, scissors);
        c->RSSetViewports(viewportCount, viewports);
        c->RSSetState(rs.Get());
        ID3D11SamplerState* sampler = psSampler.Get();
        c->PSSetSamplers(0, 1, &sampler);
        ID3D11ShaderResourceView* srv = psSrv.Get();
        c->PSSetShaderResources(0, 1, &srv);
        ID3D11Buffer* cb = psCb.Get();
        c->PSSetConstantBuffers(0, 1, &cb);
        cb = vsCb.Get();
        c->VSSetConstantBuffers(0, 1, &cb);
        c->DSSetShader(ds.Get(), dsInst.data(), dsCount);
        c->HSSetShader(hs.Get(), hsInst.data(), hsCount);
        c->GSSetShader(gs.Get(), gsInst.data(), gsCount);
        c->PSSetShader(ps.Get(), psInst.data(), psCount);
        c->VSSetShader(vs.Get(), vsInst.data(), vsCount);
        c->IASetIndexBuffer(ib.Get(), ibFormat, ibOffset);
        ID3D11Buffer* buffer = vb.Get();
        c->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        c->IASetPrimitiveTopology(topology);
        c->IASetInputLayout(layout.Get());
        for (auto* p : rtvs) { if (p) { p->Release(); } }
        auto release = [](auto& list, UINT count) { for (UINT i = 0; i < count && i < list.size(); ++i) { if (list[i]) { list[i]->Release(); } } };
        release(vsInst, vsCount); release(psInst, psCount); release(gsInst, gsCount);
        release(hsInst, hsCount); release(dsInst, dsCount);
    }
};

std::vector<std::uint8_t> RasteriseAtlas()
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(dd::kAtlasWidth) * dd::kAtlasHeight, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) { return {}; }
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), dd::kAtlasWidth, -dd::kAtlasHeight, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) { if (bitmap) { DeleteObject(bitmap); } DeleteDC(dc); return {}; }
    const HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    std::memset(bits, 0, static_cast<std::size_t>(dd::kAtlasWidth) * dd::kAtlasHeight * 4);
    // The largest bold monospace face whose cell fits the atlas cell.
    HFONT font = nullptr;
    TEXTMETRICW metrics{};
    for (int size = dd::kCellHeight; size >= 12 && !font; size -= 1) {
        HFONT candidate = CreateFontW(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (!candidate) { continue; }
        const HGDIOBJ previous = SelectObject(dc, candidate);
        SIZE extent{};
        GetTextMetricsW(dc, &metrics);
        GetTextExtentPoint32W(dc, L"W", 1, &extent);
        SelectObject(dc, previous);
        if (metrics.tmHeight <= dd::kCellHeight - 2 && extent.cx <= dd::kCellWidth - 1) { font = candidate; }
        else { DeleteObject(candidate); }
    }
    if (font) {
        const HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        GetTextMetricsW(dc, &metrics);
        for (int code = 33; code < 127; ++code) {
            const int index = code - 32;
            SIZE extent{};
            const wchar_t ch = static_cast<wchar_t>(code);
            GetTextExtentPoint32W(dc, &ch, 1, &extent);
            const int x = (index % dd::kAtlasColumns) * dd::kCellWidth + (dd::kCellWidth - extent.cx) / 2;
            const int y = (index / dd::kAtlasColumns) * dd::kCellHeight + (dd::kCellHeight - metrics.tmHeight) / 2;
            TextOutW(dc, x, y, &ch, 1);
        }
        GdiFlush();
        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        for (std::size_t i = 0; i < out.size(); ++i) { out[i] = pixels[i * 4 + 1]; }
        SelectObject(dc, oldFont);
        DeleteObject(font);
    } else {
        out.clear();
    }
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return out;
}

class Renderer {
public:
    bool Ready(ID3D11Device* device)
    {
        if (device_.Get() != device) { Reset(); device_ = device; }
        if (attempted_) { return ok_; }
        attempted_ = true;
        ok_ = Create();
        if (!ok_) { lifecycle::Log("preyvr_debug_overlay result=failed detail=create"); }
        return ok_;
    }
    bool Draw(ID3D11DeviceContext* context, ID3D11Texture2D* target, const std::vector<dd::Vertex>& vertices,
              float width, float height)
    {
        ID3D11RenderTargetView* rtv = View(target);
        if (!rtv || vertices.empty()) { return rtv != nullptr; }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto count = static_cast<UINT>(std::min(vertices.size(), kMaxVertices));
        if (FAILED(context->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return false; }
        std::memcpy(mapped.pData, vertices.data(), count * sizeof(dd::Vertex));
        context->Unmap(vertexBuffer_.Get(), 0);
        const float constants[4] = {width, height, srgb_ ? 1.0f : 0.0f, 0};
        context->UpdateSubresource(constants_.Get(), 0, nullptr, constants, 0, 0);

        SavedState saved(context);
        context->SetPredication(nullptr, FALSE);
        const UINT stride = sizeof(dd::Vertex), offset = 0;
        ID3D11Buffer* vb = vertexBuffer_.Get();
        context->IASetInputLayout(layout_.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context->VSSetShader(vs_.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(ps_.Get(), nullptr, 0);
        ID3D11Buffer* cb = constants_.Get();
        context->VSSetConstantBuffers(0, 1, &cb);
        context->PSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srv = atlasView_.Get();
        context->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* sampler = sampler_.Get();
        context->PSSetSamplers(0, 1, &sampler);
        context->RSSetState(rasterizer_.Get());
        const D3D11_VIEWPORT viewport{0, 0, width, height, 0, 1};
        context->RSSetViewports(1, &viewport);
        const float factor[4]{};
        context->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFFu);
        context->OMSetDepthStencilState(depth_.Get(), 0);
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->Draw(count, 0);
        return true;
    }
    void Reset()
    {
        views_.clear();
        device_.Reset(); vs_.Reset(); ps_.Reset(); layout_.Reset(); vertexBuffer_.Reset(); constants_.Reset();
        blend_.Reset(); rasterizer_.Reset(); depth_.Reset(); sampler_.Reset(); atlas_.Reset(); atlasView_.Reset();
        attempted_ = ok_ = false;
    }

private:
    bool Create()
    {
        ComPtr<ID3DBlob> vsCode, psCode, error;
        if (FAILED(D3DCompile(kShaders, sizeof(kShaders) - 1, "PreyVRDebugOverlay", nullptr, nullptr, "vs", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsCode, &error)) ||
            FAILED(D3DCompile(kShaders, sizeof(kShaders) - 1, "PreyVRDebugOverlay", nullptr, nullptr, "ps", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psCode, &error))) {
            if (error) {
                lifecycle::Log(std::string("preyvr_debug_overlay shader_error=") +
                               static_cast<const char*>(error->GetBufferPointer()));
            }
            return false;
        }
        if (FAILED(device_->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs_)) ||
            FAILED(device_->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps_))) {
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        if (FAILED(device_->CreateInputLayout(elements, 4, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout_))) {
            return false;
        }
        D3D11_BUFFER_DESC vb{};
        vb.ByteWidth = static_cast<UINT>(kMaxVertices * sizeof(dd::Vertex));
        vb.Usage = D3D11_USAGE_DYNAMIC;
        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 16;
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&vb, nullptr, &vertexBuffer_)) || FAILED(device_->CreateBuffer(&cb, nullptr, &constants_))) {
            return false;
        }
        D3D11_BLEND_DESC blend{};
        auto& rt = blend.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
        rt.DestBlendAlpha = D3D11_BLEND_ONE;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        // Colour only: the eye image's alpha is not ours to change.
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = FALSE;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.StencilEnable = FALSE;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateBlendState(&blend, &blend_)) || FAILED(device_->CreateRasterizerState(&raster, &rasterizer_)) ||
            FAILED(device_->CreateDepthStencilState(&depth, &depth_)) || FAILED(device_->CreateSamplerState(&sampler, &sampler_))) {
            return false;
        }
        const auto pixels = RasteriseAtlas();
        if (pixels.empty()) { return false; }
        D3D11_TEXTURE2D_DESC atlas{};
        atlas.Width = dd::kAtlasWidth;
        atlas.Height = dd::kAtlasHeight;
        atlas.MipLevels = 1;
        atlas.ArraySize = 1;
        atlas.Format = DXGI_FORMAT_R8_UNORM;
        atlas.SampleDesc.Count = 1;
        atlas.Usage = D3D11_USAGE_IMMUTABLE;
        atlas.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), static_cast<UINT>(dd::kAtlasWidth), 0};
        return SUCCEEDED(device_->CreateTexture2D(&atlas, &data, &atlas_)) &&
               SUCCEEDED(device_->CreateShaderResourceView(atlas_.Get(), nullptr, &atlasView_));
    }
    // A render target view per eye image, kept while that image lives.
    ID3D11RenderTargetView* View(ID3D11Texture2D* target)
    {
        for (auto& v : views_) { if (v.texture.Get() == target) { srgb_ = v.srgb; return v.view.Get(); } }
        D3D11_TEXTURE2D_DESC d{};
        target->GetDesc(&d);
        if (!(d.BindFlags & D3D11_BIND_RENDER_TARGET) || d.SampleDesc.Count != 1) { return nullptr; }
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = d.Format;
        switch (d.Format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: rtv.Format = DXGI_FORMAT_R10G10B10A2_UNORM; break;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: rtv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        default: break;
        }
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        Entry entry{};
        entry.texture = target;
        entry.srgb = rtv.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || rtv.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (FAILED(device_->CreateRenderTargetView(target, &rtv, &entry.view))) { return nullptr; }
        if (views_.size() >= 4) { views_.erase(views_.begin()); }
        views_.push_back(entry);
        srgb_ = entry.srgb;
        return views_.back().view.Get();
    }

    struct Entry { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> view; bool srgb = false; };
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vertexBuffer_, constants_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11DepthStencilState> depth_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Texture2D> atlas_;
    ComPtr<ID3D11ShaderResourceView> atlasView_;
    std::vector<Entry> views_;
    bool attempted_ = false, ok_ = false, srgb_ = false;
};
Renderer gRenderer;          // render thread only
std::vector<dd::Vertex> gVerticesScratch;
dd::Scene gScene;

std::string Fixed(float value, int decimals)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(value));
    return text;
}
std::string Point(Vec3 v) { return Fixed(v.x, 3) + "," + Fixed(v.y, 3) + "," + Fixed(v.z, 3); }

}  // namespace

DWORD SetDebugOverlay(unsigned mask)
{
    gMask.store(mask & dd::kAllLayers);
    lifecycle::Log("preyvr_debug_overlay result=0 mask=" + std::to_string(mask & dd::kAllLayers));
    return 0;
}
unsigned DebugOverlayMask() { return gMask.load(); }

void DebugOverlayAim(const GameplayPoseFrame& frame, const aim::Sample& sample)
{
    const unsigned mask = gMask.load(std::memory_order_relaxed);
    if (!(mask & dd::kRays)) { return; }
    const auto& right = frame.tracking.hands[static_cast<unsigned>(Hand::right)];
    if (!IsPoseUsable(right.aimPose, right.aimValidity, kPoseAgeNs) || !Finite(sample.origin) || !Finite(sample.direction)) {
        gRight.Clear();
        return;
    }
    float distance = sample.sceneDistance;
    bool hit = distance >= 0;
    if (!hit) { hit = Hit(frame, sample.origin, sample.direction, mask, distance); }
    if (!hit) { distance = static_cast<float>(ReticleConvergenceMillimetres()) * .001f; }
    RayRecord record{};
    auto& ray = record.ray;
    ray.valid = true;
    // weaponGeneration is the equipped rig's owner for this frame, 0 with none.
    ray.role = frame.weaponGeneration ? dd::RayRole::Weapon : dd::RayRole::Pointer;
    ray.direction = EngineDirToXr(frame, sample.direction);
    if (sample.muzzleOrigin && frame.cameraCentreValid) {
        // aim.shot: the sample starts at the muzzle, a real world point. Drawn
        // with the anchor the hands are placed with, it lands on the drawn barrel.
        ray.fromMuzzle = true;
        ray.origin = EngineToXr(frame, frame.cameraCentre, sample.origin);
        ray.target = EngineToXr(frame, frame.cameraCentre, dd::Add(sample.origin, dd::Scale(sample.direction, distance)));
    } else {
        ray.origin = right.aimPose.position;
        // The aim sample's ray starts at the native eye (or the hand with
        // aim.origin); its target is converted with the same anchor it was built from.
        ray.target = EngineToXr(frame, frame.nativeEye, dd::Add(sample.origin, dd::Scale(sample.direction, distance)));
    }
    ray.hit = hit;
    ray.distance = dd::Length(dd::Sub(ray.target, ray.origin));
    record.ns = MonotonicNanoseconds();
    record.reference = frame.referenceGeneration;
    if (Finite(ray.target)) { gRight.Publish(record); } else { gRight.Clear(); }
}

void DebugOverlayGameFrame(const GameplayPoseFrame& frame, bool tracking)
{
    const unsigned mask = gMask.load(std::memory_order_relaxed);
    if (!(mask & dd::kRays)) { return; }
    ShotTrace trace{};
    if (frame.cameraCentreValid && TryGetLastShot(trace) && trace.valid) {
        ShotRecord record{};
        auto& s = record.shot;
        s.valid = true;
        s.spawn = EngineToXr(frame, frame.cameraCentre, trace.spawn);
        s.target = EngineToXr(frame, frame.cameraCentre, trace.target);
        s.hit = trace.entityHit;
        s.projectileSeen = trace.projectileSeen;
        s.angleDegrees = trace.angleDegrees;
        s.spawnOffAimMm = trace.spawnOffLineMm;
        s.route = shot::RouteName(trace.route);
        s.spawnKind = shot::SpawnName(trace.spawnKind);
        record.shotNs = trace.ns;
        record.ns = MonotonicNanoseconds();
        record.reference = frame.referenceGeneration;
        gShot.Publish(record);
    }
    const auto& left = frame.tracking.hands[static_cast<unsigned>(Hand::left)];
    if (!tracking || frame.twoHand.held || !IsPoseUsable(left.aimPose, left.aimValidity, kPoseAgeNs)) {
        gLeft.Clear();
        return;
    }
    const Vec3 forward = Rotate(Normalize(left.aimPose.orientation), {0, 0, -1});
    const Vec3 anchor = frame.cameraCentreValid ? frame.cameraCentre : frame.nativeEye;
    const Vec3 originWorld = animik::ControllerWorldFromHead(frame.yaw, anchor, frame.tracking.head, left.aimPose).position;
    const bool psi = PsiTargetMode() == 2;
    float distance = 0;
    const bool hit = Finite(originWorld) && Hit(frame, originWorld, XrDirToEngine(frame, forward), mask, distance);
    if (!hit) { distance = psi ? static_cast<float>(ReticleConvergenceMillimetres()) * .001f : kPointerRange; }
    RayRecord record{};
    auto& ray = record.ray;
    ray.valid = true;
    ray.role = psi ? dd::RayRole::Psi : dd::RayRole::Pointer;
    ray.origin = left.aimPose.position;
    ray.direction = forward;
    ray.target = dd::Add(ray.origin, dd::Scale(forward, distance));
    ray.hit = hit;
    ray.distance = distance;
    record.ns = MonotonicNanoseconds();
    record.reference = frame.referenceGeneration;
    gLeft.Publish(record);
}

void DebugOverlayForegrip(const GameplayPoseFrame& frame, const twohand::Input& input, const twohand::Output& output,
                          bool regionReady)
{
    const unsigned mask = gMask.load(std::memory_order_relaxed);
    if (!(mask & dd::kForegrip)) { return; }
    if (!regionReady || !Finite(input.primary) || !Finite(input.visualPrimaryOffset)) { gForegrip.Clear(); return; }
    const Vec3 anchor = frame.cameraCentreValid ? frame.cameraCentre : frame.nativeEye;
    const Vec3 base = dd::Add(input.primary, input.visualPrimaryOffset);
    const Quaternion aim = Normalize(input.aim.orientation);
    auto toXr = [&](Vec3 local) { return EngineToXr(frame, anchor, dd::Add(base, Rotate(aim, local))); };
    ForegripRecord record{};
    auto& g = record.grip;
    g.valid = true;
    g.start = toXr(input.region.start);
    g.end = toXr(input.region.end);
    g.radius = input.region.radius;
    Vec3 closest{};
    g.inRegion = twohand::ClosestGrip(input, closest);
    g.held = output.held;
    g.blend = output.blend;
    g.socket = toXr(output.socket);
    record.ns = MonotonicNanoseconds();
    record.reference = frame.referenceGeneration;
    if (Finite(g.start) && Finite(g.end)) { gForegrip.Publish(record); } else { gForegrip.Clear(); }
}

void DebugOverlayHolsters(bool live, const std::array<Vec3, 2>& centres, Vec3 hand, bool gripPressed, bool armed,
                          bool owned)
{
    if (!(gMask.load(std::memory_order_relaxed) & dd::kBody)) { return; }
    SlotsRecord record{};
    for (int n = 0; n < 2; ++n) {
        auto& s = record.slots[n];
        s.valid = live && Finite(centres[n]);
        s.centre = centres[n];
        s.radius = equipment::HolsterGesture::Radius;
        s.hand = hand;
        s.gripPressed = gripPressed;
        s.armed = armed;
        s.owned = owned && dd::Length(dd::Sub(hand, centres[n])) <= s.radius * 1.5f;
    }
    record.ns = MonotonicNanoseconds();
    gHolsters.Publish(record);
}

void DebugOverlayHolsterSlots(bool hipStored, bool chestStored)
{
    gHipStored.store(hipStored);
    gChestStored.store(chestStored);
}

void DebugOverlayMedkit(bool live, Vec3 centre, Vec3 hand, bool gripPressed, bool armed, bool owned, bool triggerArmed)
{
    if (!(gMask.load(std::memory_order_relaxed) & dd::kBody)) { return; }
    MedkitRecord record{};
    auto& s = record.slot;
    s.valid = live && Finite(centre);
    s.centre = centre;
    s.radius = equipment::HolsterGesture::Radius;
    s.hand = hand;
    s.gripPressed = gripPressed;
    s.armed = armed;
    s.owned = owned;
    s.triggerArmed = triggerArmed;
    record.ns = MonotonicNanoseconds();
    gMedkit.Publish(record);
}

void DebugOverlayNotice(equipment::SlotNotice notice)
{
    int slot = -1;
    const auto kind = ToNotice(notice, slot);
    if (slot < 0) { return; }
    gNoticeKind[slot].store(static_cast<int>(kind));
    gNoticeNs[slot].store(MonotonicNanoseconds());
}

void DebugOverlayWristDecision(bool gate, bool visible)
{
    if (!gMask.load(std::memory_order_relaxed)) { return; }
    gWristGate.store(gate);
    gWristVisible.store(visible);
    gWristNs.store(MonotonicNanoseconds());
}

void DrawDebugOverlay(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* eyeImage,
                      const RenderContract& contract, const float* shownTangents)
{
    const unsigned mask = gMask.load(std::memory_order_relaxed);
    if (!mask || !device || !context || !eyeImage) { return; }
    if (!gRenderer.Ready(device)) { ++gFailures; return; }
    D3D11_TEXTURE2D_DESC desc{};
    eyeImage->GetDesc(&desc);
    dd::EyeView eye{};
    eye.pose = contract.pose;
    eye.left = contract.tangents[0];
    eye.right = contract.tangents[1];
    eye.up = contract.tangents[2];
    eye.down = contract.tangents[3];
    eye.width = static_cast<float>(desc.Width);
    eye.height = static_cast<float>(desc.Height);
    if (shownTangents) { dd::SetShownFrustum(eye, shownTangents[0], shownTangents[1], shownTangents[2], shownTangents[3]); }
    dd::OverlayFrame frame = Gather(mask);
    dd::BuildOverlayScene(frame, mask, gScene);
    gVerticesScratch.clear();
    const auto stats = dd::Tessellate(gScene, eye, gVerticesScratch, kMaxVertices);
    if (!gRenderer.Draw(context, eyeImage, gVerticesScratch, eye.width, eye.height)) {
        ++gFailures;
        static bool logged = false;
        if (!logged) { logged = true; lifecycle::Log("preyvr_debug_overlay result=refused detail=eye_image_not_render_target"); }
        return;
    }
    ++gDrawn;
    gVertices.store(gVerticesScratch.size());
    if (stats.truncated) { ++gTruncated; }
    std::lock_guard lock(gLastMutex);
    gLastFrame = std::move(frame);
    gLastEye = eye;
    gLastEyeIndex = contract.eye;
    gLastStats = stats;
}

void ReleaseDebugOverlay()
{
    gRenderer.Reset();
    gVerticesScratch = {};
    gScene.Clear();
}

std::string DebugOverlayReport()
{
    std::ostringstream out;
    out << " mask=" << gMask.load() << " drawn=" << gDrawn.load() << " vertices=" << gVertices.load()
        << " truncated=" << gTruncated.load() << " failures=" << gFailures.load() << " queries=" << gQueries.load()
        << " sceneFault=" << SceneQueryFaulted();
    std::lock_guard lock(gLastMutex);
    out << " eye=" << gLastEyeIndex << " lines=" << gLastStats.lines << " spheres=" << gLastStats.spheres
        << " markers=" << gLastStats.markers << " labels=" << gLastStats.labels << " culled=" << gLastStats.culled << "\n"
        << dd::OverlayStatusText(gLastFrame, gMask.load());
    return out.str();
}

std::string DebugOverlayMarks()
{
    std::lock_guard lock(gLastMutex);
    std::ostringstream out;
    const auto& f = gLastFrame;
    auto projected = [&](Vec3 p) -> std::string {
        const auto q = dd::Project(gLastEye, p);
        return q ? " px " + Fixed(q->x, 1) + " " + Fixed(q->y, 1) + " depth " + Fixed(q->depth, 3) : " px -";
    };
    out << "eye " << gLastEyeIndex << " size " << gLastEye.width << "x" << gLastEye.height << " pose "
        << Point(gLastEye.pose.position) << "\n";
    const char* hands[2] = {"L", "R"};
    for (int h = 0; h < 2; ++h) {
        const auto& r = f.rays[h];
        if (!r.valid) { continue; }
        out << "ray " << hands[h] << " role " << static_cast<int>(r.role) << " hit " << r.hit << " distance "
            << Fixed(r.distance, 3) << " origin " << Point(r.origin) << projected(r.origin) << " target "
            << Point(r.target) << projected(r.target) << "\n";
    }
    auto slot = [&](const dd::Slot& s) {
        if (!s.valid) { return; }
        out << "slot " << s.name << " centre " << Point(s.centre) << projected(s.centre) << " hand " << Point(s.hand)
            << " cm " << Fixed(dd::SlotHandDistance(s) * 100, 1) << " armed " << s.armed << " owned " << s.owned
            << " stored " << s.stored << "\n";
    };
    if (f.holstersEnabled) { slot(f.holsters[0]); slot(f.holsters[1]); }
    if (f.medkitEnabled) { slot(f.medkit); }
    if (f.wrist.enabled && Finite(f.wrist.card.position)) {
        out << "wrist card " << Point(f.wrist.card.position) << projected(f.wrist.card.position) << " gate "
            << f.wrist.gate << " visible " << f.wrist.visible << " facing " << Fixed(f.wrist.facing, 3) << " viewing "
            << Fixed(f.wrist.viewing, 3) << " distance " << Fixed(f.wrist.distance, 3) << "\n";
    }
    if (f.shot.valid) {
        out << "shot " << f.shot.route << " age " << Fixed(f.shot.age, 2) << " spawn " << Point(f.shot.spawn)
            << projected(f.shot.spawn) << " target " << Point(f.shot.target) << projected(f.shot.target) << " angle "
            << Fixed(f.shot.angleDegrees, 3) << " offAimMm " << Fixed(f.shot.spawnOffAimMm, 1) << " spawnKind "
            << f.shot.spawnKind << " seen " << f.shot.projectileSeen << "\n";
    }
    if (f.foregrip.valid) {
        out << "foregrip start " << Point(f.foregrip.start) << projected(f.foregrip.start) << " end "
            << Point(f.foregrip.end) << projected(f.foregrip.end) << " in " << f.foregrip.inRegion << " held "
            << f.foregrip.held << "\n";
    }
    for (int h = 0; h < 2; ++h) {
        if (f.hands[h].valid) {
            out << "grip " << hands[h] << " " << Point(f.hands[h].grip.position) << projected(f.hands[h].grip.position)
                << " aim " << Point(f.hands[h].aim.position) << "\n";
        }
    }
    return out.str();
}

}  // namespace preyvr::dll
