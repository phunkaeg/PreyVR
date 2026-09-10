#include "preyvr/InputEvent.h"

#include <cmath>
#include <cstring>

namespace preyvr::input {
namespace {

// Field offsets, named so a mistake reads as a mistake rather than as a number.
constexpr std::size_t kOffsetDevice = 0x00;
constexpr std::size_t kOffsetState = 0x04;
constexpr std::size_t kOffsetInputChar = 0x08;
constexpr std::size_t kOffsetKeyName = 0x10;
constexpr std::size_t kOffsetKeyId = 0x18;
constexpr std::size_t kOffsetModifiers = 0x1C;
constexpr std::size_t kOffsetValue = 0x20;
constexpr std::size_t kOffsetSymbol = 0x28;
constexpr std::size_t kOffsetDeviceIndex = 0x30;

// Static storage, because the refire path retains the pointer. These are the
// names the native XInput producer registers.
struct NamedKey {
    int keyId;
    const char* name;
};

constexpr NamedKey kNames[] = {
    {kMouseX, "maxis_x"},
    {kMouse3, "mouse3"},
    {kDPadUp, "xi_dpad_up"},
    {kDPadDown, "xi_dpad_down"},
    {kDPadLeft, "xi_dpad_left"},
    {kDPadRight, "xi_dpad_right"},
    {kStart, "xi_start"},
    {kBack, "xi_back"},
    {kShoulderL, "xi_shoulderl"},
    {kShoulderR, "xi_shoulderr"},
    {kButtonA, "xi_a"},
    {kButtonB, "xi_b"},
    {kButtonX, "xi_x"},
    {kButtonY, "xi_y"},
    {kThumbLX, "xi_thumblx"},
    {kThumbLY, "xi_thumbly"},
    {kTriggerL, "xi_triggerl"},
    {kTriggerR, "xi_triggerr"},
    {kThumbRX, "xi_thumbrx"},
    {kThumbRY, "xi_thumbry"},
    // **The trigger BUTTONS, which are separate keys from the analog axes.**
    // Firing is bound to the button, not the axis, and 0x21D was refused 299
    // times in a headset purely because this table did not name it -- the id was
    // correct and read from the target, the mod's own guard rejected it.
    // Names read from the registration at `FUN_1809D9EF0` (R-115).
    {kTriggerLButton, "xi_triggerl_btn"},
    {kTriggerRButton, "xi_triggerr_btn"},
    {kThumbRLeft, "xi_thumbr_left"},
    {kThumbRRight, "xi_thumbr_right"},
    // Keyboard names as CryEngine's keyboard device registers them.
    {kKeyEscape, "escape"},
    {kKeyW, "w"},
    {kKeyEnter, "enter"},
    {kKeyA, "a"},
    {kKeyS, "s"},
    {kKeyD, "d"},
    {kKeySpace, "space"},
    {kKeyUp, "up"},
    {kKeyLeft, "left"},
    {kKeyRight, "right"},
    {kKeyDown, "down"},
};

} // namespace

std::uint32_t DeviceForKeyId(int keyId)
{
    if (keyId < 0x100) {
        return kDeviceKeyboard;
    }
    if (keyId < 0x200) {
        return kDeviceMouse;
    }
    return kDeviceGamepad;
}

std::uint16_t InputCharForKeyId(int keyId)
{
    switch (keyId) {
        case kKeyEnter: return 13;    // CR, what a Return key contributes
        case kKeySpace: return 32;
        case kKeyEscape: return 27;
        case kKeyW: return 'w';
        case kKeyA: return 'a';
        case kKeyS: return 's';
        case kKeyD: return 'd';
        default: return 0;            // arrows and pad buttons contribute no char
    }
}

const char* KeyNameFor(int keyId)
{
    for (const NamedKey& entry : kNames) {
        if (entry.keyId == keyId) {
            return entry.name;
        }
    }
    return nullptr;
}

bool BuildEvent(const EventFields& fields, std::uint8_t* out, std::size_t capacity)
{
    if (out == nullptr || capacity < kEventSize) {
        return false;
    }
    if (fields.keyName == nullptr || fields.keyName[0] == '\0') {
        return false;
    }
    if (!std::isfinite(fields.value)) {
        return false;
    }
    // PostInputEvent rejects an unknown key id outside a UI event, so producing
    // one here would be building something that is refused downstream and
    // reported as if it had been sent.
    if (fields.keyId == -1 && fields.state != kStateUI) {
        return false;
    }

    // Zeroed whole, not field by field. The struct has three padding regions and
    // the refire path copies all seven qwords, so an unwritten gap travels.
    std::memset(out, 0, kEventSize);

    std::memcpy(out + kOffsetDevice, &fields.device, sizeof(fields.device));
    std::memcpy(out + kOffsetState, &fields.state, sizeof(fields.state));

    std::memcpy(out + kOffsetInputChar, &fields.inputChar, sizeof(fields.inputChar));

    const char* name = fields.keyName;
    std::memcpy(out + kOffsetKeyName, &name, sizeof(name));

    std::memcpy(out + kOffsetKeyId, &fields.keyId, sizeof(fields.keyId));
    std::memcpy(out + kOffsetModifiers, &fields.modifiers, sizeof(fields.modifiers));
    std::memcpy(out + kOffsetValue, &fields.value, sizeof(fields.value));

    // Explicitly null rather than merely left zero, so the intent is on the page:
    // a fabricated symbol would be read as a real one.
    const void* symbol = nullptr;
    std::memcpy(out + kOffsetSymbol, &symbol, sizeof(symbol));

    out[kOffsetDeviceIndex] = fields.deviceIndex;
    return true;
}

int MenuActionKeyId(MenuAction action)
{
    switch (action) {
        case MenuAction::Up: return kDPadUp;
        case MenuAction::Down: return kDPadDown;
        case MenuAction::Left: return kDPadLeft;
        case MenuAction::Right: return kDPadRight;
        case MenuAction::Accept: return kButtonA;
        case MenuAction::Cancel: return kButtonB;
        case MenuAction::Start: return kStart;
        case MenuAction::PreviousTab: return kShoulderL;
        case MenuAction::NextTab: return kShoulderR;
        case MenuAction::Secondary: return kButtonX;
        case MenuAction::Tertiary: return kButtonY;
        case MenuAction::PreviousPage: return kTriggerLButton;
        case MenuAction::NextPage: return kTriggerRButton;
    }
    return -1;
}

unsigned int BuildMenuTap(MenuAction action, std::uint8_t* out, std::size_t capacity)
{
    if (out == nullptr || capacity < kEventSize * 2) {
        return 0;
    }
    const int keyId = MenuActionKeyId(action);
    const char* const name = KeyNameFor(keyId);
    if (keyId < 0 || name == nullptr) {
        return 0;
    }

    EventFields press;
    press.device = kDeviceGamepad;
    press.state = kStatePressed;
    press.keyName = name;
    press.keyId = keyId;
    press.value = 1.0f;
    if (!BuildEvent(press, out, capacity)) {
        return 0;
    }

    EventFields release = press;
    release.state = kStateReleased;
    release.value = 0.0f;
    if (!BuildEvent(release, out + kEventSize, capacity - kEventSize)) {
        return 0;
    }
    return 2;
}

} // namespace preyvr::input
