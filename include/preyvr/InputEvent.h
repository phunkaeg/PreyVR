#pragma once

#include <cstddef>
#include <cstdint>

// Building Prey's native `SInputEvent` as bytes, so the layout can be tested
// before anything is posted into a live engine.
//
// **Why this is its own layer.** Posting a malformed event calls into the
// engine's own input pipeline with a struct it will read seven qwords out of. A
// field at the wrong offset is not a wrong answer, it is an arbitrary pointer
// dereference inside `SendEventToListeners`. The construction is therefore pure
// and fixture-tested, and only the call itself lives in the DLL.
//
// **Layout (R-089), byte-proven by producer/consumer agreement** across
// `PostInputEvent` `0x9D6D30`, `SendEventToListeners` `0x9D7430`, XInput update
// `0x9DAA20`, action matching `0x3CEA70` and the refire copy `0x3D2360`, which
// copies seven qwords:
//
// | offset | size | field                                        |
// |--------|------|----------------------------------------------|
// | `0x00` | 4    | deviceType                                   |
// | `0x04` | 4    | state                                        |
// | `0x08` | 2    | wchar_t inputChar                            |
// | `0x10` | 8    | pointer to a NUL-terminated key name         |
// | `0x18` | 4    | keyId                                        |
// | `0x1C` | 4    | modifiers                                    |
// | `0x20` | 4    | float value                                  |
// | `0x28` | 8    | SInputSymbol*                                |
// | `0x30` | 1    | deviceIndex                                  |
//
// **CryEngine 5's public layout puts the key name at `0x08` and is wrong here.**
// This project has twice been burned by treating a modern header as the target
// rather than as an oracle, so the offsets above come from the disassembly and
// the header is used only for vocabulary.
namespace preyvr::input {

inline constexpr std::size_t kEventSize = 0x38;

enum Device : std::uint32_t {
    kDeviceKeyboard = 0,
    kDeviceMouse = 1,
    kDeviceJoystick = 2,
    kDeviceGamepad = 3,
};

enum State : std::uint32_t {
    kStatePressed = 1,
    kStateReleased = 2,
    kStateDown = 4,
    kStateChanged = 8,
    kStateUI = 16,
};

// `EKeyId`, from the PDB-derived header. These are compile-time constants rather
// than addresses, so unlike an RVA they carry across builds -- and the one value
// the disassembly proves independently, `xi_thumblx = 0x210`, matches the header
// exactly, which is what makes the neighbours usable.
inline constexpr int kDPadUp = 0x200;
inline constexpr int kDPadDown = 0x201;
inline constexpr int kDPadLeft = 0x202;
inline constexpr int kDPadRight = 0x203;
inline constexpr int kStart = 0x204;
inline constexpr int kBack = 0x205;
inline constexpr int kShoulderL = 0x208;
inline constexpr int kShoulderR = 0x209;
inline constexpr int kButtonA = 0x20A;
inline constexpr int kButtonB = 0x20B;
inline constexpr int kButtonX = 0x20C;
inline constexpr int kButtonY = 0x20D;
inline constexpr int kThumbLX = 0x210;
inline constexpr int kThumbLY = 0x211;
// Right stick and triggers. **No longer inferred: read from this build**
// (R-115). `FUN_1809D9EF0` registers every device key as a name/id pair, and the
// ids below are the `MOV R8D, <id>` that follows each `LEA RAX, [<name>]`:
//
//   xi_triggerr      0x20F      xi_thumbr_up     0x218
//   xi_thumblx       0x210      xi_thumbr_down   0x219
//   xi_thumbly       0x211      xi_thumbr_left   0x21A
//   xi_thumbl_up     0x212      xi_thumbr_right  0x21B
//   xi_thumbl_down   0x213      xi_triggerl_btn  0x21C
//   xi_thumbl_left   0x214      xi_triggerr_btn  0x21D
//   xi_thumbl_right  0x215
//   xi_thumbrx       0x216
//   xi_thumbry       0x217
//
// The reading validates itself: xi_thumblx and xi_thumbly come out as 0x210 and
// 0x211, which R-089 had already confirmed live. Every previously inferred value
// was correct.
//
// **The trigger has TWO keys, and that is what cost a headset session.** 0x20F
// is the analog axis; 0x21D is the digital button, and firing is bound to the
// button. Posting only the axis was accepted by PostInputEvent -- 36 presses,
// zero refusals -- and did nothing at all, because a valid key that nothing is
// bound to is indistinguishable from a working one in the counters. The
// fail-closed refusal cannot catch this: the key is real, it is just not the
// one that fires.
inline constexpr int kTriggerL = 0x20E;
inline constexpr int kTriggerR = 0x20F;
inline constexpr int kThumbRX = 0x216;
inline constexpr int kThumbRY = 0x217;
inline constexpr int kThumbRLeft = 0x21A;
inline constexpr int kThumbRRight = 0x21B;
inline constexpr int kTriggerLButton = 0x21C;
inline constexpr int kTriggerRButton = 0x21D;

// Keyboard, same enum. `eKI_W = 0x10` is corroborated independently by the
// disassembly (R-089 reads `moveforward` bound to w / 0x10), which is a second
// cross-check on this header after `xi_thumblx = 0x210`.
inline constexpr int kKeyEscape = 0x00;
inline constexpr int kKeyW = 0x10;
inline constexpr int kKeyEnter = 0x1B;
inline constexpr int kKeyA = 0x1D;
inline constexpr int kKeyS = 0x1E;
inline constexpr int kKeyD = 0x1F;
inline constexpr int kKeySpace = 0x38;
inline constexpr int kKeyUp = 0x61;
inline constexpr int kKeyLeft = 0x63;
inline constexpr int kKeyRight = 0x64;
inline constexpr int kKeyDown = 0x66;

// **The device follows the key id, it is not a caller's choice.** The id ranges
// are the enum's own partition -- keyboard below `0x100`, mouse `0x100..0x1FF`,
// XInput from `0x200` -- and the native producers set the matching device. A
// keyboard key posted as a gamepad event is a combination no real device
// produces, and `SendEventToListeners` filters on device.
std::uint32_t DeviceForKeyId(int keyId);

// The character a key contributes to a UI event, or 0 when it contributes none.
// Only keys the front end plausibly consumes are mapped; an unmapped key gets 0,
// which is the honest answer rather than an invented code point.
std::uint16_t InputCharForKeyId(int keyId);

struct EventFields {
    std::uint32_t device = kDeviceGamepad;
    std::uint32_t state = kStatePressed;
    // **The Flash UI reads this and nothing else.** The menu listener's
    // `OnInputEventUI` requires `state == kStateUI` and then dispatches
    // `*(uint16*)(event+0x08)` straight to Scaleform -- it never looks at
    // `keyId` or `keyName`. Every event this project sent carried zero here,
    // because R-089 said the *movement* producer does not need the field and
    // that advice was generalised to every consumer. A menu tap with a zero
    // char is a keystroke the UI cannot see.
    std::uint16_t inputChar = 0;
    // **Must have static storage.** The action-refire path keeps a copy of the
    // event including its pointers, so a name on the caller's stack becomes a
    // dangling read at an arbitrary later frame -- the kind of fault that lands
    // nowhere near the code that caused it.
    const char* keyName = nullptr;
    int keyId = 0;
    int modifiers = 0;
    float value = 0.0f;
    std::uint8_t deviceIndex = 0;
};

// Writes exactly `kEventSize` bytes into `out`.
//
// Refuses rather than producing a questionable event: a null or empty key name
// (the action manager hashes input names, and refire retains the pointer), a
// non-finite value, an unknown key id of -1 outside a UI event (which
// `PostInputEvent` rejects anyway), or too small a buffer. The symbol pointer is
// written as null on purpose -- `PostInputEvent` tolerates it and skips the
// symbol-held bookkeeping, and a fabricated `SInputSymbol` would be read as a
// real one.
bool BuildEvent(const EventFields& fields, std::uint8_t* out, std::size_t capacity);

// --- menu navigation ---------------------------------------------------------
//
// Not a testing convenience: the product's end state is "no mouse", so a menu
// that only answers to a keyboard is an unfinished mod. Driving it through the
// engine's own input layer is what a controller binding *is*, and it makes the
// menus reachable from the same place everything else is driven from.
enum class MenuAction {
    Up,
    Down,
    Left,
    Right,
    Accept,
    Cancel,
    Start,
};

// A digital tap is two events: pressed, then released. Emitting only the press
// leaves the engine believing the button is still held, which in a menu repeats
// the action and in gameplay is a stuck input.
//
// Writes two consecutive `kEventSize` blocks and returns the count, or 0 if
// refused. `capacity` is in bytes.
unsigned int BuildMenuTap(MenuAction action, std::uint8_t* out, std::size_t capacity);

// The key id a menu action maps to, or -1 when unmapped.
int MenuActionKeyId(MenuAction action);

// The stable key name for a key id, or nullptr when this layer does not know it.
// Static storage by construction, which is what the refire path requires.
const char* KeyNameFor(int keyId);

} // namespace preyvr::input
