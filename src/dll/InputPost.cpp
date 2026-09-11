#include "InputPost.h"

#include "CameraEditHook.h"
#include "InputPathProbe.h"
#include "Logger.h"
#include "HudBridge.h"

#include "preyvr/InputEvent.h"
#include "preyvr/InputQueue.h"
#include "preyvr/MenuTapDispatch.h"
#include <cstring>

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
std::atomic<unsigned long long> gMenuDiscarded{0};
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
// True while this thread is inside a PostInputEvent WE issued. The engine's own
// input walk runs synchronously inside that call, so a player-side handler that
// fires while this is set was driven by us; one that fires while it is clear was
// driven by real hardware. That distinction is what makes the double-driving
// check a number instead of a feeling -- see the fleet playbook's
// "double-driving trap", whose whole point is `engineLeaked`.
thread_local bool tPostingOurEvent = false;

bool CallPost(PostInputEventFn function, void* input, const void* event, bool force)
{
    __try {
        tPostingOurEvent = true;
        function(input, event, force);
        tPostingOurEvent = false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        tPostingOurEvent = false;
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
    static input::MenuTapDispatch menuDispatch;
    std::uint64_t scope=0;
    int key=-1;unsigned state=0;
    bool accepted=false;
    for(unsigned i=0;i<input::kQueueCapacity;++i) {
        if(!gQueue.Pop(event.data(),&scope))return;
        std::memcpy(&key,event.data()+input::kOffsetKeyId,sizeof(key));
        std::memcpy(&state,event.data()+input::kOffsetState,sizeof(state));
        if(menuDispatch.Allow(scope,HudMenuEpoch(),HudMenuStateKnown()&&HudMenuIsOpen(),key,state)) {
            accepted=true;break;
        }
        gMenuDiscarded.fetch_add(1,std::memory_order_relaxed);
    }
    if(!accepted)return;
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
    menuDispatch.Delivered(scope,key,state);
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

DWORD PostMenuAction(unsigned int action, unsigned long long menuEpoch)
{
    if (action > static_cast<unsigned int>(input::MenuAction::NextPage)) {
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
    const auto scope=menuEpoch;
    if(scope && (scope!=HudMenuEpoch() || !HudMenuStateKnown() || !HudMenuIsOpen()))return 4;
    if (!gEnabled.load(std::memory_order_acquire) || !gQueue.PushPair(buffer.data(),scope)) {
        return 4;
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

DWORD PostRawInputImmediate(int keyId, unsigned int state, int valueMilli)
{
    // **Only from the drain thread**, which is the engine's own input thread.
    // The queue exists so that a producer on any thread can hand work to that
    // one; a caller already on it does not need it, and for an analog axis the
    // queue is actively wrong: it drains ONE event per frame so a menu press and
    // its release cannot collapse, while this lane produces up to two per frame.
    // Queued, the axes outrun the drain, the ring fills and every later event is
    // dropped -- measured as movePosted=0 against moveDropped=109 in 4 s.
    //
    // Collapsing is correct for an axis in a way it is not for a button: an
    // older stick reading is simply a worse answer to the same question.
    if (!gEnabled.load(std::memory_order_acquire) || gPost == nullptr || gInput == nullptr) {
        return 1;
    }
    const unsigned long thread = GetCurrentThreadId();
    const unsigned long drain = gDrainThread.load(std::memory_order_acquire);
    if (drain != 0 && thread != drain) {
        return 5;   // wrong thread: refuse rather than post from anywhere
    }
    const char* const name = input::KeyNameFor(keyId);
    if (name == nullptr) { return 2; }
    input::EventFields fields;
    fields.device = input::DeviceForKeyId(keyId);
    fields.state = state;
    fields.inputChar = input::InputCharForKeyId(keyId);
    fields.keyName = name;
    fields.keyId = keyId;
    fields.value = static_cast<float>(valueMilli) / 1000.0f;
    std::array<std::uint8_t, input::kEventSize> buffer{};
    if (!input::BuildEvent(fields, buffer.data(), buffer.size())) { return 3; }
    if (!CallPost(gPost, gInput, buffer.data(), gForce.load(std::memory_order_acquire))) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        gEnabled.store(false, std::memory_order_release);
        Log("result=failed detail=exception_in_immediate_post disarmed=1");
        return 4;
    }
    gPosted.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

unsigned long long InputPostCount() { return gPosted.load(std::memory_order_relaxed); }
unsigned long long InputPostRefusedCount() { return gRefused.load(std::memory_order_relaxed); }
unsigned long long InputMenuDiscardedCount() { return gMenuDiscarded.load(std::memory_order_relaxed); }
unsigned long long InputQueueDroppedCount() { return gQueue.Dropped(); }
unsigned long long InputQueueDepthEstimate() { return gQueue.Pushed() - gQueue.Popped(); }
bool InputPostDrivingThisThread() { return tPostingOurEvent; }

unsigned long InputDrainThreadId() { return gDrainThread.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
