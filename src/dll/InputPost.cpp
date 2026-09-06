#include "InputPost.h"

#include "CameraEditHook.h"
#include "InputPathProbe.h"
#include "Logger.h"

#include "preyvr/InputEvent.h"
#include "preyvr/InputQueue.h"

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
std::atomic<unsigned long> gDrainThread{0};
// **Diagnostic.** `PostInputEvent` checks posting-enabled first and drops the
// event when it is off; `force` bypasses that check. The native producers do not
// force, so this is not the shipping default -- it exists to tell "the engine
// never saw it" apart from "the engine saw it and did not act", which counters
// on our side cannot distinguish.
std::atomic<bool> gForce{false};

input::EventQueue gQueue;

// Resolved once and cached, so the drain does no discovery work inside a frame.
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
    // **Without the drain seam this would queue for ever.** Arming the post and
    // leaving delivery to a hook someone else might install is how the first
    // live run reported `result=0` for a tap that never reached the engine.
    if (EnsureRenderHookInstalled() != 0) {
        Log("result=refused detail=render_hook_unavailable");
        return false;
    }
    gInput = reinterpret_cast<void*>(input);
    gPost = reinterpret_cast<PostInputEventFn>(reinterpret_cast<std::uintptr_t>(preyDll) + rva);
    Log("result=0 detail=resolved slot=" + std::to_string(InputPathAlignmentSlot()));
    return true;
}

// The guarded call, kept as a leaf with no C++ objects in scope: a function that
// unwinds objects cannot host `__try`, and hoisting the logging out is what keeps
// the guard where the fault would actually happen.
//
// `force` is false. The native producer does not force, and forcing would bypass
// the engine's own posting-enabled check.
bool CallPost(PostInputEventFn function, void* input, const void* event, bool force)
{
    __try {
        function(input, event, force);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Queued, never called here. The producer may be any thread; the engine's input
// walk belongs to one.
bool Enqueue(const std::uint8_t* event)
{
    if (!gEnabled.load(std::memory_order_acquire)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return gQueue.Push(event);
}

} // namespace

void DrainQueuedInput()
{
    if (!gEnabled.load(std::memory_order_acquire) || gPost == nullptr || gInput == nullptr) {
        return;
    }

    // Recorded once so the ownership claim in the header can be *checked* against
    // a live run rather than believed. If this ever prints a thread that is not
    // the engine's main thread, the seam is wrong however well the counters read.
    const unsigned long thread = GetCurrentThreadId();
    unsigned long expected = 0;
    if (gDrainThread.compare_exchange_strong(expected, thread)) {
        Log("result=0 detail=drain_thread id=" + std::to_string(thread));
    }

    // **One per frame.** A menu that samples input once per frame would see a
    // press and its release collapse into one update and could conclude the
    // button was never held. Real input arrives on separate frames.
    std::array<std::uint8_t, input::kEventSize> event{};
    if (!gQueue.Pop(event.data())) {
        return;
    }
    if (!CallPost(gPost, gInput, event.data(), gForce.load(std::memory_order_acquire))) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        // Disarmed rather than retried: a fault here means the call target or the
        // event shape is wrong, and repeating it every frame would turn one bad
        // assumption into a crash loop.
        gEnabled.store(false, std::memory_order_release);
        Log("result=failed detail=exception_in_post disarmed=1");
        return;
    }
    gPosted.fetch_add(1, std::memory_order_relaxed);
}

DWORD SetInputPostForce(unsigned int force)
{
    gForce.store(force != 0u, std::memory_order_release);
    Log(std::string("result=0 detail=force value=") + (force ? "1" : "0"));
    return 0;
}

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
    // Both or neither. A press queued without its release would leave the engine
    // believing the button is held for as long as the queue stays short.
    if (!Enqueue(buffer.data())) {
        return 4;
    }
    if (!Enqueue(buffer.data() + input::kEventSize)) {
        Log("result=failed detail=release_not_queued keyid=" +
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
    // Derived, not assumed: a keyboard key posted as a gamepad event is a pairing
    // no real device produces, and the listener walk filters on device.
    fields.device = input::DeviceForKeyId(keyId);
    fields.state = state;
    // Carried for every state, not only UI: the field costs nothing on the
    // paths that ignore it, and omitting it is what made the UI path silent.
    fields.inputChar = input::InputCharForKeyId(keyId);
    fields.keyName = name;
    fields.keyId = keyId;
    fields.value = static_cast<float>(valueMilli) / 1000.0f;

    std::array<std::uint8_t, input::kEventSize> buffer{};
    if (!input::BuildEvent(fields, buffer.data(), buffer.size())) {
        return 3;
    }
    return Enqueue(buffer.data()) ? 0u : 4u;
}

unsigned long long InputPostCount() { return gPosted.load(std::memory_order_relaxed); }
unsigned long long InputPostRefusedCount() { return gRefused.load(std::memory_order_relaxed); }
unsigned long long InputQueueDroppedCount() { return gQueue.Dropped(); }
unsigned long long InputQueueDepthEstimate() { return gQueue.Pushed() - gQueue.Popped(); }
unsigned long InputDrainThreadId() { return gDrainThread.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
