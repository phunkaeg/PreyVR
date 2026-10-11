#include "DualOverlay.h"

#include "HudLayer.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrSessionHost.h"
#include "preyvr/EngineMap.h"

#include <MinHook.h>
#include <d3d11.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_dual_overlay " + line);
}

// RT_Draw2dImageInternal(this, S2DImage* images, uint32 count), landmark
// renderer.draw2d_images. The fourth register is passed through untouched
// rather than assumed unused.
using Draw2dImagesFn = void(__fastcall*)(void* renderer, void* images, unsigned int count, void* passThrough);
constexpr std::uintptr_t kDraw2dImagesRva = 0xF34C70;
// S2DImage as RT_Draw2dImage packs it: texture +0x00, x/y/w/h at +0x10..+0x1C,
// packed ARGB colour at +0x38. Read for the census only.
constexpr std::size_t kImageRect = 0x10;
constexpr std::size_t kImageColour = 0x38;

std::mutex gInstallMutex;
bool gInstalled = false;
std::atomic<Draw2dImagesFn> gDraw2dImages{nullptr};
std::atomic<bool> gEnabled{false};

std::atomic<unsigned long long> gImageDraws{0}, gImagesRedrawn{0}, gImagesRefused{0};
std::atomic<const char*> gLastImageRefusal{"none"};
std::atomic<unsigned int> gImagesLogged{0};

struct MovieCount {
    char name[40]{};
    unsigned long long redrawn = 0, refused = 0;
    const char* lastRefusal = "none";
    bool loggedRedrawn = false, loggedRefused = false;
};
std::mutex gMovieMutex;
std::array<MovieCount, 24> gMovies{};

bool ReadImage(const void* images, float (&rect)[4], unsigned int& colour, std::uintptr_t& texture)
{
    __try {
        const auto* image = static_cast<const std::uint8_t*>(images);
        std::memcpy(&texture, image, sizeof(texture));
        std::memcpy(rect, image + kImageRect, sizeof(rect));
        std::memcpy(&colour, image + kImageColour, sizeof(colour));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The first images of the session, so a fade can be recognised by its rect
// (Prey's faders draw 0,0,800,600) and colour.
void NoteImages(const void* images, unsigned int count, bool redrawn, const char* refusal)
{
    if (gImagesLogged.fetch_add(1, std::memory_order_relaxed) >= 12) {
        return;
    }
    float rect[4]{};
    unsigned int colour = 0;
    std::uintptr_t texture = 0;
    std::ostringstream line;
    line << "image count=" << count;
    if (ReadImage(images, rect, colour, texture)) {
        line << " rect=" << rect[0] << ',' << rect[1] << ',' << rect[2] << ',' << rect[3] << " argb=0x"
             << std::hex << colour << std::dec << " textured=" << (texture != 0 ? 1 : 0);
    }
    line << " redrawn=" << (redrawn ? 1 : 0) << " refusal=" << (refusal != nullptr ? refusal : "none");
    Log(line.str());
}

struct ImageCall {
    Draw2dImagesFn draw;
    void* renderer;
    void* images;
    unsigned int count;
    void* passThrough;
};

void DrawImages(void* argument)
{
    auto* call = static_cast<ImageCall*>(argument);
    call->draw(call->renderer, call->images, call->count, call->passThrough);
}

void __fastcall Draw2dImagesDetour(void* renderer, void* images, unsigned int count, void* passThrough)
{
    const auto original = gDraw2dImages.load(std::memory_order_acquire);
    original(renderer, images, count, passThrough);
    // Two loads when off, or outside a held frame's overlay stretch.
    if (!gEnabled.load(std::memory_order_acquire) || !DualOverlayWindowOpen() || images == nullptr || count == 0) {
        return;
    }
    gImageDraws.fetch_add(1, std::memory_order_relaxed);
    auto* device = *reinterpret_cast<ID3D11Device**>(reinterpret_cast<std::uintptr_t>(renderer) +
                                                     engine::RendererLayout::device);
    ID3D11DeviceContext* context = nullptr;
    if (device != nullptr) device->GetImmediateContext(&context);
    ID3D11RenderTargetView* target = nullptr;
    ID3D11DepthStencilView* depth = nullptr;
    if (context != nullptr) context->OMGetRenderTargets(1, &target, &depth);
    const char* refusal = target == nullptr ? "no_target" : nullptr;
    ID3D11RenderTargetView* held = target != nullptr ? HeldFirstEyeTarget(target, &refusal) : nullptr;
    bool redrawn = false;
    if (held != nullptr) {
        // The same images, same state, into the held eye. Any rebind of the
        // backbuffer inside the draw lands on the eye too (DrawRedirected).
        ImageCall call{original, renderer, images, count, passThrough};
        redrawn = DrawRedirected(context, target, depth, held, depth, &DrawImages, &call);
        if (!redrawn) refusal = "redirect_unavailable";
    }
    if (redrawn) {
        gImagesRedrawn.fetch_add(1, std::memory_order_relaxed);
    } else {
        gImagesRefused.fetch_add(1, std::memory_order_relaxed);
        gLastImageRefusal.store(refusal, std::memory_order_relaxed);
    }
    NoteImages(images, count, redrawn, refusal);
    if (depth != nullptr) depth->Release();
    if (target != nullptr) target->Release();
    if (context != nullptr) context->Release();
}

DWORD EnsureImageHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled) {
        return 0;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=preydll_not_loaded");
        return 3;
    }
    const engine::Landmark* landmark = nullptr;
    for (const auto& candidate : engine::Landmarks()) {
        if (candidate.id == std::string_view("renderer.draw2d_images")) landmark = &candidate;
    }
    if (landmark == nullptr || landmark->rva != kDraw2dImagesRva) {
        Log("result=unavailable detail=landmark_missing id=renderer.draw2d_images");
        return 3;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(preyDll) + landmark->rva;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0 ||
        std::memcmp(reinterpret_cast<const void*>(address), landmark->expected.data(), landmark->expected.size()) != 0) {
        Log("result=unavailable detail=prologue_mismatch id=renderer.draw2d_images");
        return 4;
    }
    if (!EnsureMinHook()) {
        return 5;
    }
    void* original = nullptr;
    MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(address), reinterpret_cast<void*>(&Draw2dImagesDetour),
                                     &original);
    if (status == MH_OK) {
        gDraw2dImages.store(reinterpret_cast<Draw2dImagesFn>(original), std::memory_order_release);
        status = MH_EnableHook(reinterpret_cast<void*>(address));
        if (status != MH_OK) MH_RemoveHook(reinterpret_cast<void*>(address));
    }
    if (status != MH_OK) {
        Log(std::string("result=failed detail=create_hook minhook=") + MH_StatusToString(status));
        return 5;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed draw2dImages=0xF34C70");
    return 0;
}

} // namespace

DWORD SetDualOverlayRedraw(unsigned int enabled)
{
    if (enabled > 1) {
        return 1;
    }
    if (enabled != 0) {
        // Both halves or neither: the 2D images and the Flash movies are one
        // overlay stretch, and half of it in both eyes is still a broken frame.
        if (!EnsureOverlayHooks()) {
            Log("result=unavailable detail=flash_hooks");
            return 2;
        }
        const DWORD installed = EnsureImageHook();
        if (installed != 0) {
            return installed;
        }
    }
    gEnabled.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

bool DualOverlayRedrawEnabled()
{
    return gEnabled.load(std::memory_order_acquire);
}

void NoteFlashOverlay(const char* movie, bool redrawn, const char* refusal)
{
    if (movie == nullptr) movie = "unnamed";
    if (refusal == nullptr) refusal = "none";
    bool logRedrawn = false, logRefused = false;
    {
        std::lock_guard lock(gMovieMutex);
        MovieCount* entry = nullptr;
        for (auto& candidate : gMovies) {
            if (candidate.name[0] == 0 || std::strcmp(candidate.name, movie) == 0) {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr) {
            return; // table full: counted nowhere rather than under a wrong name
        }
        if (entry->name[0] == 0) {
            std::size_t i = 0;
            for (; i + 1 < sizeof(entry->name) && movie[i]; ++i) entry->name[i] = movie[i];
            entry->name[i] = 0;
        }
        if (redrawn) {
            ++entry->redrawn;
            logRedrawn = !entry->loggedRedrawn;
            entry->loggedRedrawn = true;
        } else {
            ++entry->refused;
            entry->lastRefusal = refusal;
            logRefused = !entry->loggedRefused;
            entry->loggedRefused = true;
        }
    }
    if (logRedrawn) Log(std::string("movie name=") + movie + " first=redrawn");
    if (logRefused) Log(std::string("movie name=") + movie + " first=refused reason=" + refusal);
}

std::string DualOverlayReport()
{
    std::ostringstream out;
    out << " overlay={enabled=" << (gEnabled.load() ? 1 : 0) << " hook=" << (gInstalled ? 1 : 0)
        << " window=" << (DualOverlayWindowOpen() ? 1 : 0) << " imageDraws=" << gImageDraws.load()
        << " imagesRedrawn=" << gImagesRedrawn.load() << " imagesRefused=" << gImagesRefused.load()
        << " lastImageRefusal=" << gLastImageRefusal.load() << " movies=";
    std::lock_guard lock(gMovieMutex);
    bool any = false;
    for (const auto& entry : gMovies) {
        if (entry.name[0] == 0) break;
        out << (any ? ";" : "") << entry.name << ':' << entry.redrawn << '/' << entry.refused;
        if (entry.refused != 0) out << '(' << entry.lastRefusal << ')';
        any = true;
    }
    if (!any) out << "none";
    out << '}';
    return out.str();
}

} // namespace preyvr::dll
