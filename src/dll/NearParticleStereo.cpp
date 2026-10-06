#include "NearParticleStereo.h"

#include "AimTakeover.h"
#include "CameraEditHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/NearParticles.h"

#include <MinHook.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

#pragma intrinsic(_ReturnAddress)

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_near_fx " + line); }

// `SParticleVertexContext::Init`: mov rax,rsp / push rbx / push rsi / push r15 /
// sub rsp,0x130 / mov rsi,[r8+0x108] / xor r15d,r15d. No relative operand in the
// 24 bytes, so the gate is exact for this build and nothing else.
constexpr std::array<std::uint8_t, 24> kInitPrologue{
    0x48, 0x8B, 0xC4, 0x53, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x01,
    0x00, 0x00, 0x49, 0x8B, 0xB0, 0x08, 0x01, 0x00, 0x00, 0x45, 0x33, 0xFF,
};
// `CParticle::GetRenderMatrix`: mov rax,rsp / mov [rax+0x20],rbx / push rbp,rsi,
// r12,r13,r15 / lea rbp,[rax-0x2f] / sub rsp,0xF0. No relative operand.
constexpr std::array<std::uint8_t, 24> kRenderMatrixPrologue{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x55, 0x56, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xD1, 0x48, 0x81, 0xEC, 0xF0, 0x00,
};
using InitFn = void(__fastcall*)(std::uint8_t* context, float maxContainerPixels, void* container);
// Ten arguments, all integer-class (pointers, and two bools passed in full stack
// slots, so their bits are forwarded unchanged).
using RenderMatrixFn = void*(__fastcall*)(void* particle, float* vX, float* vY, float* vZ, float* vT,
                                         const void* loc, const void* renderData, const std::uint8_t* context,
                                         std::uintptr_t flagA, std::uintptr_t flagB);

std::mutex gInstallMutex;
bool gInstallTried = false;
bool gInitHooked = false, gMatrixHooked = false;
std::uintptr_t gBase = 0;
std::atomic<InitFn> gInitOriginal{nullptr};
std::atomic<RenderMatrixFn> gMatrixOriginal{nullptr};
std::atomic<bool> gEnabled{true};
// Research (near.fxhide): moves every DrawNear particle 10 km away, so a capture
// shows which parts of an effect they are. Never on outside a test.
std::atomic<bool> gHide{false};
// near.fxbridge (default 1): the Q-Beam's inner beam, a flat-screen bridge
// authored behind the muzzle, is mirrored to run ahead of it
// (preyvr/NearParticles.h, BridgeShift).
std::atomic<bool> gBridge{true};
std::atomic<unsigned long long> gBridged{0}, gBridgeNoAxis{0};
std::atomic<int> gLastBridgeShiftMm{-1};

// Vertex contexts per builder (unknown, sprites, geometry, other).
struct Counters {
    std::atomic<unsigned long long> contexts{0}, drawNear{0}, corrected{0}, noEye{0};
};
std::array<Counters, 4> gCounters{};
// Sprite vertices: DrawNear positions seen, moved, and left (no stereo eye).
std::atomic<unsigned long long> gSpriteDrawNear{0}, gSpriteMoved{0}, gSpriteNoEye{0};
std::atomic<int> gLastShiftMicrometres{-1};
std::atomic<bool> gLoggedFirstGeometry{false}, gLoggedFirstSprite{false};

std::size_t Index(nearfx::Builder b) { return static_cast<std::size_t>(b); }

bool DrawNear(const std::uint8_t* context)
{
    const auto* params = context != nullptr
        ? *reinterpret_cast<const std::uint8_t* const*>(context + nearfx::kContextParams)
        : nullptr;
    return params != nullptr && params[nearfx::kParamsDrawNear] != 0;
}

bool EyeFor(const float* camera, nearfx::EyeCamera& eye, int& which)
{
    BuiltEye record{};
    if (!FindBuiltEyeByPosition(camera, record)) { return false; }
    eye = {{record.position[0], record.position[1], record.position[2]},
           {record.centre[0], record.centre[1], record.centre[2]}};
    which = record.eye;
    return true;
}

void NoteShift(Vec3 d)
{
    gLastShiftMicrometres.store(static_cast<int>(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) * 1e6f),
                                std::memory_order_relaxed);
}

// The bridge's world translation for this context, or false. The emitter's
// +Y is the barrel; the beam leaves along it, and the aim lane's direction is
// that barrel (Q-Beam measured 0.000 deg, aim.beam). Particle contexts are
// built on render threads: the last direction stays usable for 100 ms if a
// read meets the publisher.
bool BridgeFor(const std::uint8_t* context, Vec3& shift)
{
    const auto* params = *reinterpret_cast<const std::uint8_t* const*>(context + nearfx::kContextParams);
    Vec3 offset{};
    std::memcpy(&offset, params + nearfx::kParamsPositionOffset, sizeof(offset));
    if (!(offset.y <= -nearfx::kBridgeMinBack)) { return false; }
    thread_local Vec3 forward{};
    thread_local std::uint64_t forwardNs = 0;
    aim::Sample sample{};
    const std::uint64_t now = MonotonicNanoseconds();
    if (TryGetAimSample(sample) && FreshSample(now, sample.publishedNs, 200000000ull)) {
        forward = sample.direction; forwardNs = now;
    }
    if (!forwardNs || now - forwardNs > 100000000ull || !nearfx::BridgeShift(offset, forward, shift)) {
        gBridgeNoAxis.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    gBridged.fetch_add(1, std::memory_order_relaxed);
    gLastBridgeShiftMm.store(static_cast<int>(-2000.0f * offset.y), std::memory_order_relaxed);
    return true;
}

// Geometry: the context's camera becomes the cyclops before Init derives from
// it; CParticle::RenderGeometry then subtracts it for DrawNear (0x1B1A88).
void __fastcall InitHook(std::uint8_t* context, float maxContainerPixels, void* container)
{
    const InitFn original = gInitOriginal.load(std::memory_order_acquire);
    const auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - gBase);
    const nearfx::Builder builder = nearfx::BuilderFromReturnRva(rva);
    Counters& c = gCounters[Index(builder)];
    c.contexts.fetch_add(1, std::memory_order_relaxed);
    // Init itself dereferences the params on its first instructions, so reading
    // the flag here adds no access the engine is not about to make.
    if (DrawNear(context)) {
        c.drawNear.fetch_add(1, std::memory_order_relaxed);
        if (builder == nearfx::Builder::Geometry && gHide.load(std::memory_order_relaxed)) {
            reinterpret_cast<float*>(context + nearfx::kContextCameraPosition)[2] -= 10000.0f;
        } else if (builder == nearfx::Builder::Geometry && gEnabled.load(std::memory_order_acquire)) {
            auto* camera = reinterpret_cast<float*>(context + nearfx::kContextCameraPosition);
            nearfx::EyeCamera eye{};
            int which = -1;
            Vec3 origin{};
            if (EyeFor(camera, eye, which) &&
                nearfx::CameraSpaceOrigin({camera[0], camera[1], camera[2]}, eye, origin)) {
                NoteShift({origin.x - camera[0], origin.y - camera[1], origin.z - camera[2]});
                // RenderGeometry draws `position - camera`: a camera moved back
                // by `shift` draws the particle moved forward by it.
                Vec3 shift{};
                if (gBridge.load(std::memory_order_relaxed) && BridgeFor(context, shift)) {
                    origin = {origin.x - shift.x, origin.y - shift.y, origin.z - shift.z};
                }
                camera[0] = origin.x; camera[1] = origin.y; camera[2] = origin.z;
                c.corrected.fetch_add(1, std::memory_order_relaxed);
                bool expected = false;
                if (gLoggedFirstGeometry.compare_exchange_strong(expected, true)) {
                    Log("result=0 detail=first_correction path=geometry eye=" + std::to_string(which));
                }
            } else {
                c.noEye.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (original != nullptr) {
        original(context, maxContainerPixels, container);
    }
}

// Sprites: the world-space vertex position moves by the eye's offset, so the
// renderer's later `vertex - eye camera` is `world - cyclops`. The context's
// camera is the eye camera here (the sprite builder is not corrected) and
// identifies the eye.
void* __fastcall RenderMatrixHook(void* particle, float* vX, float* vY, float* vZ, float* vT, const void* loc,
                                  const void* renderData, const std::uint8_t* context, std::uintptr_t flagA,
                                  std::uintptr_t flagB)
{
    const RenderMatrixFn original = gMatrixOriginal.load(std::memory_order_acquire);
    const auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - gBase);
    void* result = original != nullptr
        ? original(particle, vX, vY, vZ, vT, loc, renderData, context, flagA, flagB)
        : nullptr;
    if (vT == nullptr || !nearfx::IsSpriteVertexCaller(rva) || !DrawNear(context)) {
        return result;
    }
    gSpriteDrawNear.fetch_add(1, std::memory_order_relaxed);
    if (gHide.load(std::memory_order_relaxed)) {
        vT[2] -= 10000.0f;
        return result;
    }
    if (!gEnabled.load(std::memory_order_acquire)) {
        return result;
    }
    const auto* camera = reinterpret_cast<const float*>(context + nearfx::kContextCameraPosition);
    nearfx::EyeCamera eye{};
    int which = -1;
    Vec3 offset{};
    if (EyeFor(camera, eye, which) && nearfx::EyeOffset({camera[0], camera[1], camera[2]}, eye, offset)) {
        vT[0] += offset.x; vT[1] += offset.y; vT[2] += offset.z;
        NoteShift(offset);
        gSpriteMoved.fetch_add(1, std::memory_order_relaxed);
        bool expected = false;
        if (gLoggedFirstSprite.compare_exchange_strong(expected, true)) {
            Log("result=0 detail=first_correction path=sprites eye=" + std::to_string(which));
        }
    } else {
        gSpriteNoEye.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

template <class Fn, std::size_t N>
bool HookAt(std::uint32_t rva, const std::array<std::uint8_t, N>& prologue, void* detour, std::atomic<Fn>& original,
            const char* name)
{
    auto* const target = reinterpret_cast<void*>(gBase + rva);
    if (std::memcmp(target, prologue.data(), prologue.size()) != 0) {
        Log(std::string("result=unavailable detail=prologue_mismatch target=") + name);
        return false;
    }
    Fn trampoline = nullptr;
    if (MH_CreateHook(target, detour, reinterpret_cast<void**>(&trampoline)) != MH_OK) {
        Log(std::string("result=failed detail=create_hook target=") + name);
        return false;
    }
    original.store(trampoline, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        Log(std::string("result=failed detail=enable_hook target=") + name);
        return false;
    }
    return true;
}

bool Install()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInitHooked && gMatrixHooked; }
    gBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if (gBase == 0) { Log("result=unavailable detail=no_preydll"); return false; }
    gInstallTried = true;
    EnsureMinHook();
    // The sprite seam first, and the geometry seam only with it: one without the
    // other would leave the halves of one effect (mesh glow, sparks) apart.
    gMatrixHooked = HookAt(nearfx::kGetRenderMatrixRva, kRenderMatrixPrologue,
                           reinterpret_cast<void*>(&RenderMatrixHook), gMatrixOriginal, "get_render_matrix");
    if (gMatrixHooked) {
        gInitHooked = HookAt(nearfx::kVertexContextInitRva, kInitPrologue, reinterpret_cast<void*>(&InitHook),
                             gInitOriginal, "vertex_context_init");
    }
    Log(std::string("result=") + (gInitHooked && gMatrixHooked ? "0" : "unavailable") +
        " sprites=" + std::to_string(gMatrixHooked) + " geometry=" + std::to_string(gInitHooked));
    return gInitHooked && gMatrixHooked;
}

} // namespace

DWORD SetNearParticleStereo(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gEnabled.store(enabled != 0, std::memory_order_release);
    // Installing for 0 too: the hooks then only count, which is how the native
    // behaviour is measured in the same session.
    if (!Install()) { return ERROR_NOT_SUPPORTED; }
    Log(std::string("result=0 detail=enabled value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetNearParticleBridge(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gBridge.store(enabled != 0, std::memory_order_relaxed);
    Log(std::string("result=0 detail=bridge value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetNearParticleHide(unsigned hide)
{
    if (hide > 1) { return ERROR_INVALID_PARAMETER; }
    if (!Install()) { return ERROR_NOT_SUPPORTED; }
    gHide.store(hide != 0, std::memory_order_relaxed);
    Log(std::string("result=0 detail=hide value=") + (hide ? "1" : "0"));
    return 0;
}

unsigned NearParticleStereoEnabled() { return gEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }

void EnsureNearParticleStereo()
{
    // PREYVR_NEAR_FX=0 (launcher -NearFx 0): leave the particle code untouched,
    // not even hooked.
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"PREYVR_NEAR_FX", value, 8) > 0 && value[0] == L'0') {
        gEnabled.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    if (gEnabled.load(std::memory_order_acquire)) { Install(); }
}

std::string NearParticleStereoReport()
{
    std::ostringstream s;
    s << "nearFx=" << NearParticleStereoEnabled() << " nearFxHooks=" << (gMatrixHooked ? 1 : 0)
      << (gInitHooked ? 1 : 0);
    const char* names[] = {"Unknown", "Sprites", "Geometry", "Other"};
    for (std::size_t i = 0; i < gCounters.size(); ++i) {
        const Counters& c = gCounters[i];
        s << " nearFx" << names[i] << "=" << c.contexts.load(std::memory_order_relaxed) << "/"
          << c.drawNear.load(std::memory_order_relaxed) << "/" << c.corrected.load(std::memory_order_relaxed)
          << "/" << c.noEye.load(std::memory_order_relaxed);
    }
    s << " nearFxSpriteVerts=" << gSpriteDrawNear.load(std::memory_order_relaxed) << "/"
      << gSpriteMoved.load(std::memory_order_relaxed) << "/" << gSpriteNoEye.load(std::memory_order_relaxed)
      << " nearFxBridge=" << (gBridge.load(std::memory_order_relaxed) ? 1 : 0) << "/"
      << gBridged.load(std::memory_order_relaxed) << "/" << gBridgeNoAxis.load(std::memory_order_relaxed)
      << " nearFxBridgeShiftMm=" << gLastBridgeShiftMm.load(std::memory_order_relaxed)
      << " nearFxShiftUm=" << gLastShiftMicrometres.load(std::memory_order_relaxed)
      << " (contexts/drawNear/corrected/noEye; verts drawNear/moved/noEye; bridge on/mirrored/noAxis)";
    return s.str();
}

} // namespace preyvr::dll
