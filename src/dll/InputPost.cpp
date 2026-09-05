#include "InputPost.h"

#include "InputPathProbe.h"
#include "Logger.h"

#include "preyvr/InputEvent.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_input_post " + line);
}

// RCX input, RDX event, R8B force. Native return is void.
using PostInputEventFn = void(__fastcall*)(void*, const void*, bool);

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gPosted{0};
std::atomic<unsigned long long> gRefused{0};

// Resolved once and cached, so a per-event call does no discovery work.
void* gInput = nullptr;
PostInputEventFn gPost = nullptr;

bool Resolve()
{
    if (gInput != nullptr && gPost != nullptr) {
        return true;
    }
    if (ResolveInputPath() != 0) {
        Log("result=unavailable detail=input_path_unresolved");
        return false;
    }
    // **The gate that matters.** The header-derived slot index is a guess, and
    // the probe exists precisely because Prey is an Arkane fork of an older
    // CryEngine where the modern touch-event pair may be absent. Calling through
    // an unmeasured index is calling an arbitrary virtual on a live object.
    if (InputPathAlignmentConfirmed() == 0) {
        Log("result=refused detail=vtable_alignment_unconfirmed");
        return false;
    }
    const auto input = static_cast<std::uintptr_t>(InputPathInputPointer());
    const auto rva = static_cast<std::uintptr_t>(InputPathPostInputEventRva());
    if (input == 0 || rva == 0) {
        Log("result=refused detail=null_input_or_rva");
        return false;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    gInput = reinterpret_cast<void*>(input);
    gPost = reinterpret_cast<PostInputEventFn>(reinterpret_cast<std::uintptr_t>(preyDll) + rva);
    Log("result=0 detail=resolved slot=" + std::to_string(InputPathAlignmentSlot()));
    return true;
}

// The guarded call, kept as a leaf with no C++ objects in scope: a function that
// unwinds objects cannot host `__try`, and hoisting the logging out is what
// keeps the guard where the fault would actually happen.
//
// `force` is false. The native producer does not force, and forcing would bypass
// the engine's own posting-enabled check.
bool CallPost(PostInputEventFn function, void* input, const void* event)
{
    __try {
        function(input, event, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PostOne(const std::uint8_t* event)
{
    if (!gEnabled.load(std::memory_order_acquire) || gPost == nullptr || gInput == nullptr) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!CallPost(gPost, gInput, event)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        // Disarmed rather than retried: a fault here means the call target or the
        // event shape is wrong, and repeating it every frame would turn one bad
        // assumption into a crash loop.
        gEnabled.store(false, std::memory_order_release);
        Log("result=failed detail=exception_in_post disarmed=1");
        return false;
    }
    gPosted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

} // namespace

DWORD SetInputPostEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on && !Resolve()) {
        return 1;
    }
    gEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

DWORD PostMenuAction(unsigned int action)
{
    if (action > static_cast<unsigned int>(input::MenuAction::Start)) {
        return 2;
    }
    std::array<std::uint8_t, input::kEventSize * 2> buffer{};
    const unsigned int count = input::BuildMenuTap(static_cast<input::MenuAction>(action),
                                                   buffer.data(), buffer.size());
    if (count != 2) {
        return 3;
    }
    // Press and release both, or neither is meaningful. If the press lands and
    // the release does not, the engine believes the button is still held.
    if (!PostOne(buffer.data())) {
        return 4;
    }
    if (!PostOne(buffer.data() + input::kEventSize)) {
        Log("result=failed detail=release_not_posted keyid=" +
            std::to_string(input::MenuActionKeyId(static_cast<input::MenuAction>(action))));
        return 5;
    }
    return 0;
}

DWORD PostRawInput(int keyId, unsigned int state, int valueMilli)
{
    const char* const name = input::KeyNameFor(keyId);
    if (name == nullptr) {
        // Refused rather than invented: the key name must be stable storage the
        // refire path can keep, and a caller-supplied string is not.
        return 2;
    }
    input::EventFields fields;
    fields.device = input::kDeviceGamepad;
    fields.state = state;
    fields.keyName = name;
    fields.keyId = keyId;
    fields.value = static_cast<float>(valueMilli) / 1000.0f;

    std::array<std::uint8_t, input::kEventSize> buffer{};
    if (!input::BuildEvent(fields, buffer.data(), buffer.size())) {
        return 3;
    }
    return PostOne(buffer.data()) ? 0u : 4u;
}

unsigned long long InputPostCount() { return gPosted.load(std::memory_order_relaxed); }
unsigned long long InputPostRefusedCount() { return gRefused.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
