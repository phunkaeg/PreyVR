#include "FrameCaptureWin32.h"

#include "Logger.h"
#include "HudLayer.h"
#include "preyvr/EngineMap.h"
#include "preyvr/FrameDump.h"

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <mutex>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace preyvr::dll {
namespace {

std::atomic<bool> gRequestPending{false};
std::atomic<std::uint32_t> gRequestTag{0};
std::atomic<bool> gHudOnly{false};
std::mutex gRequestMutex;
std::atomic<DWORD> gLastResult{static_cast<DWORD>(FrameCaptureResult::ok)};
std::atomic<unsigned long long> gCompleted{0};
std::atomic<int> gTagOverride{-1};

void Finish(FrameCaptureResult result, const char* detail)
{
    gLastResult.store(static_cast<DWORD>(result), std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_frame_capture result=" << static_cast<DWORD>(result)
         << " detail=" << detail;
    lifecycle::Log(line.str());
}

// Env var first so a test run can direct dumps somewhere disposable; otherwise
// beside this DLL, which is predictable without configuration.
std::filesystem::path CaptureDirectory()
{
    wchar_t fromEnv[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"PREYVR_CAPTURE_DIR", fromEnv, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(fromEnv);
    }

    HMODULE self = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CaptureDirectory),
            &self) == 0 ||
        self == nullptr) {
        return std::filesystem::path(L".");
    }
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(self, modulePath, MAX_PATH) == 0) {
        return std::filesystem::path(L".");
    }
    return std::filesystem::path(modulePath).parent_path();
}

bool WriteDump(
    const framedump::Header& header,
    const std::uint8_t* source,
    std::uint32_t sourceRowPitch,
    const std::filesystem::path& path)
{
    std::vector<std::uint8_t> headerBytes(framedump::kHeaderSize);
    if (!framedump::EncodeHeader(header, headerBytes)) {
        return false;
    }

    // Created here rather than assumed. PREYVR_CAPTURE_DIR can name a directory
    // that does not exist yet, and fopen will not make one -- which surfaced as
    // `write_failed` on a live run with no hint that a missing directory was the
    // whole story.
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        return false;
    }

    bool ok = std::fwrite(headerBytes.data(), 1, headerBytes.size(), file) == headerBytes.size();
    // Rows are written at the header's declared pitch, which is the pitch D3D11
    // gave us. Re-packing here would mean the file no longer matches what was
    // mapped, and a mismatch between claimed and actual pitch is exactly the
    // corruption the decoder is written to catch.
    for (std::uint32_t y = 0; ok && y < header.height; ++y) {
        const std::uint8_t* row = source + static_cast<std::size_t>(y) * sourceRowPitch;
        ok = std::fwrite(row, 1, header.rowPitch, file) == header.rowPitch;
    }
    std::fclose(file);
    if (!ok) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    return ok;
}

} // namespace

DWORD RequestFrameCapture(std::uint32_t tag,bool hudOnly)
{
    std::lock_guard lock(gRequestMutex);
    if (gRequestPending.load(std::memory_order_acquire)) {
        return static_cast<DWORD>(FrameCaptureResult::refused);
    }
    gRequestTag.store(tag, std::memory_order_release);
    gHudOnly.store(hudOnly);
    gRequestPending.store(true,std::memory_order_release);
    return static_cast<DWORD>(FrameCaptureResult::ok);
}

void SetFrameCaptureTagOverride(int tag)
{
    gTagOverride.store(tag, std::memory_order_release);
}

DWORD LastFrameCaptureResult()
{
    return gLastResult.load(std::memory_order_acquire);
}

unsigned long long CompletedFrameCaptureCount()
{
    return gCompleted.load(std::memory_order_acquire);
}

void ServiceFrameCapture(void* renderer, unsigned long long frameIndex)
{
    if (!gRequestPending.load(std::memory_order_acquire)) {
        return; // the hot path: one relaxed load and out
    }

    const int override = gTagOverride.load(std::memory_order_acquire);
    const std::uint32_t tag = override >= 0 ? static_cast<std::uint32_t>(override)
                                            : gRequestTag.load(std::memory_order_acquire);
    // Clear the request whatever happens below, so a failing capture cannot arm
    // itself again every frame and turn one bad request into a permanent stall.
    struct RequestGuard {
        ~RequestGuard() { gRequestPending.store(false, std::memory_order_release); }
    } guard;

    if (renderer == nullptr) {
        Finish(FrameCaptureResult::unavailable, "null_renderer");
        return;
    }

    const auto rendererAddress = reinterpret_cast<std::uintptr_t>(renderer);
    auto* swapChain = *reinterpret_cast<IDXGISwapChain**>(
        rendererAddress + engine::RendererLayout::swapchain);
    auto* device = *reinterpret_cast<ID3D11Device**>(
        rendererAddress + engine::RendererLayout::device);
    if (swapChain == nullptr || device == nullptr) {
        Finish(FrameCaptureResult::unavailable, "swapchain_or_device_null");
        return;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if(gHudOnly.load()) {
        backBuffer=HudLayerTexture();
        if(!backBuffer) {Finish(FrameCaptureResult::unavailable,"no_fresh_hud_texture");return;}
        backBuffer->AddRef();
    } else if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backBuffer))) ||
        backBuffer == nullptr) {
        Finish(FrameCaptureResult::failed, "get_buffer_failed");
        return;
    }

    D3D11_TEXTURE2D_DESC description{};
    backBuffer->GetDesc(&description);

    // The live swapchain has SampleDesc.Count == 1, so CopyResource is enough.
    // If that ever changes this must resolve first, and silently copying a
    // multisampled surface would produce a corrupt dump rather than an error.
    if (description.SampleDesc.Count != 1) {
        backBuffer->Release();
        Finish(FrameCaptureResult::unavailable, "multisampled_backbuffer_needs_resolve");
        return;
    }

    D3D11_TEXTURE2D_DESC stagingDescription = description;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&stagingDescription, nullptr, &staging)) ||
        staging == nullptr) {
        backBuffer->Release();
        Finish(FrameCaptureResult::failed, "create_staging_failed");
        return;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (context == nullptr) {
        staging->Release();
        backBuffer->Release();
        Finish(FrameCaptureResult::unavailable, "no_immediate_context");
        return;
    }

    context->CopyResource(staging, backBuffer);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) || mapped.pData == nullptr) {
        context->Release();
        staging->Release();
        backBuffer->Release();
        Finish(FrameCaptureResult::failed, "map_failed");
        return;
    }

    framedump::Header header{};
    header.width = description.Width;
    header.height = description.Height;
    header.dxgiFormat = static_cast<std::uint32_t>(description.Format);
    header.rowPitch = mapped.RowPitch;
    header.frameIndex = frameIndex;
    header.tag = tag;

    std::ostringstream name;
    name << "frame-" << frameIndex << "-tag" << tag << ".pvrframe";
    const std::filesystem::path path = CaptureDirectory() / name.str();

    const bool written = WriteDump(
        header, static_cast<const std::uint8_t*>(mapped.pData), mapped.RowPitch, path);

    context->Unmap(staging, 0);
    context->Release();
    staging->Release();
    backBuffer->Release();

    if (!written) {
        Finish(FrameCaptureResult::failed, "write_failed");
        return;
    }

    gCompleted.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream line;
    line << "preyvr_frame_capture result=0 frame=" << frameIndex
         << " tag=" << tag
         << " width=" << header.width
         << " height=" << header.height
         << " format=" << header.dxgiFormat
         << " rowPitch=" << header.rowPitch
         << " path=" << path.string();
    lifecycle::Log(line.str());
    gLastResult.store(static_cast<DWORD>(FrameCaptureResult::ok), std::memory_order_release);
}

} // namespace preyvr::dll
