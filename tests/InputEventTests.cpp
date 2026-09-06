#include "preyvr/InputEvent.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

using namespace preyvr::input;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

template <typename T>
T ReadAt(const std::uint8_t* buffer, std::size_t offset)
{
    T value{};
    std::memcpy(&value, buffer + offset, sizeof(T));
    return value;
}

// Every field is checked at its byte offset, because a field at the wrong offset
// is not a wrong answer -- it is an arbitrary pointer dereference inside the
// engine's own listener walk. This is the fixture that has to be right before
// anything is posted.
void TestEventLandsAtTheDocumentedOffsets()
{
    std::uint8_t buffer[kEventSize];
    std::memset(buffer, 0xCD, sizeof(buffer));   // poison: prove every byte is written

    EventFields fields;
    fields.device = kDeviceGamepad;
    fields.state = kStateChanged;
    fields.keyName = "xi_thumblx";
    fields.keyId = kThumbLX;
    fields.modifiers = 0;
    fields.value = -0.5f;
    fields.deviceIndex = 0;

    Require(BuildEvent(fields, buffer, sizeof(buffer)), "a well formed event must build");

    Require(ReadAt<std::uint32_t>(buffer, 0x00) == 3u, "device gamepad at 0x00");
    Require(ReadAt<std::uint32_t>(buffer, 0x04) == 8u, "state changed at 0x04");
    Require(ReadAt<std::uint16_t>(buffer, 0x08) == 0u, "inputChar zeroed at 0x08");

    // The name is a POINTER at 0x10, not inline characters. CryEngine 5's public
    // layout puts it at 0x08; using that here would hand the engine a pointer
    // assembled from the device and state fields.
    const char* const name = ReadAt<const char*>(buffer, 0x10);
    Require(name != nullptr && std::strcmp(name, "xi_thumblx") == 0,
            "the key name pointer must be at 0x10 and point at the name");

    Require(ReadAt<std::int32_t>(buffer, 0x18) == 0x210, "keyId at 0x18");
    Require(ReadAt<std::int32_t>(buffer, 0x1C) == 0, "modifiers at 0x1C");
    Require(ReadAt<float>(buffer, 0x20) == -0.5f, "value at 0x20");
    Require(ReadAt<const void*>(buffer, 0x28) == nullptr, "symbol pointer null at 0x28");
    Require(buffer[0x30] == 0, "deviceIndex at 0x30");

    // The padding regions must be cleared, because the refire path copies all
    // seven qwords and an unwritten gap travels with the copy.
    Require(buffer[0x0A] == 0 && buffer[0x0F] == 0, "the 0x0A padding must be zeroed");
    Require(buffer[0x24] == 0 && buffer[0x27] == 0, "the 0x24 padding must be zeroed");
    Require(buffer[0x37] == 0, "the tail padding must be zeroed");
}

void TestRefusalsRatherThanQuestionableEvents()
{
    std::uint8_t buffer[kEventSize];
    EventFields fields;
    fields.keyName = "xi_a";
    fields.keyId = kButtonA;

    EventFields noName = fields;
    noName.keyName = nullptr;
    Require(!BuildEvent(noName, buffer, sizeof(buffer)),
            "a null key name must be refused: refire retains the pointer");
    noName.keyName = "";
    Require(!BuildEvent(noName, buffer, sizeof(buffer)), "an empty key name must be refused");

    EventFields bad = fields;
    bad.value = std::numeric_limits<float>::quiet_NaN();
    Require(!BuildEvent(bad, buffer, sizeof(buffer)), "a NaN value must be refused");
    bad.value = std::numeric_limits<float>::infinity();
    Require(!BuildEvent(bad, buffer, sizeof(buffer)), "an infinite value must be refused");

    // PostInputEvent rejects an unknown key id outside a UI event, so building
    // one would be reporting a send that the engine discards.
    EventFields unknown = fields;
    unknown.keyId = -1;
    Require(!BuildEvent(unknown, buffer, sizeof(buffer)),
            "key id -1 must be refused outside a UI event");
    unknown.state = kStateUI;
    Require(BuildEvent(unknown, buffer, sizeof(buffer)), "but a UI event may carry it");

    Require(!BuildEvent(fields, buffer, kEventSize - 1), "a short buffer must be refused");
    Require(!BuildEvent(fields, nullptr, kEventSize), "a null buffer must be refused");
}

void TestKeyNamesAreStableStorage()
{
    // The pointer handed to the engine must outlive the call. Two lookups
    // returning the same address is the observable form of that guarantee.
    const char* first = KeyNameFor(kButtonA);
    const char* second = KeyNameFor(kButtonA);
    Require(first != nullptr, "a known key must have a name");
    Require(first == second, "the same key must yield the same stable pointer");
    Require(std::strcmp(first, "xi_a") == 0, "and the name the native producer registers");
    Require(KeyNameFor(0x7777) == nullptr, "an unknown key id must have no name");

    // The one value the disassembly proves independently.
    Require(std::strcmp(KeyNameFor(kThumbLX), "xi_thumblx") == 0,
            "the byte-proven axis name must match");
}

void TestDeviceFollowsTheKeyIdRange()
{
    // The enum partitions the id space and the native producers set the matching
    // device. Posting a keyboard key as a gamepad event is a pairing no real
    // device produces, and the listener walk filters on device.
    Require(DeviceForKeyId(kKeySpace) == kDeviceKeyboard, "space is a keyboard key");
    Require(DeviceForKeyId(kKeyEscape) == kDeviceKeyboard, "escape is at id 0 and still keyboard");
    Require(DeviceForKeyId(kKeyW) == kDeviceKeyboard, "w is a keyboard key");
    Require(DeviceForKeyId(0x10D) == kDeviceMouse, "the 0x100 block is the mouse");
    Require(DeviceForKeyId(kThumbLX) == kDeviceGamepad, "the 0x200 block is XInput");
    Require(DeviceForKeyId(kButtonA) == kDeviceGamepad, "including the face buttons");

    // And the keyboard names must be present, or a raw post is refused rather
    // than inventing a string the refire path would keep a pointer to.
    Require(KeyNameFor(kKeySpace) != nullptr, "space must have a stable name");
    Require(KeyNameFor(kKeyEnter) != nullptr, "enter must have a stable name");
}

// **The field the Flash UI actually reads.** Its OnInputEventUI dispatches
// *(uint16*)(event+0x08) to Scaleform and never consults keyId or keyName, so a
// UI event carrying zero here is a keystroke the menu cannot see. Every event
// this project sent before 2026-09-07 carried zero.
void TestInputCharIsWrittenAtOffsetEight()
{
    std::uint8_t buffer[kEventSize];
    std::memset(buffer, 0xCD, sizeof(buffer));
    EventFields fields;
    fields.state = kStateUI;
    fields.keyName = "enter";
    fields.keyId = kKeyEnter;
    fields.inputChar = InputCharForKeyId(kKeyEnter);
    Require(fields.inputChar == 13, "Return contributes a carriage return");
    Require(BuildEvent(fields, buffer, sizeof(buffer)), "a UI event must build");
    Require(ReadAt<std::uint16_t>(buffer, 0x08) == 13, "the char lands at 0x08");
    Require(ReadAt<std::uint32_t>(buffer, 0x04) == kStateUI, "and the state is UI");

    Require(InputCharForKeyId(kKeySpace) == 32, "space contributes a space");
    Require(InputCharForKeyId(kKeyEscape) == 27, "escape contributes ESC");
    // Arrows and pad buttons contribute no character; inventing one would be
    // guessing at a code point the front end never sends.
    Require(InputCharForKeyId(kKeyUp) == 0, "an arrow key contributes no char");
    Require(InputCharForKeyId(kButtonA) == 0, "a pad button contributes no char");

    // A default-constructed event still carries zero, so nothing that does not
    // ask for a char silently acquires one.
    EventFields plain;
    plain.keyName = "xi_a";
    plain.keyId = kButtonA;
    Require(BuildEvent(plain, buffer, sizeof(buffer)), "a plain event builds");
    Require(ReadAt<std::uint16_t>(buffer, 0x08) == 0, "and carries no char");
}

void TestMenuTapIsPressThenRelease()
{
    std::uint8_t buffer[kEventSize * 2];
    std::memset(buffer, 0xCD, sizeof(buffer));

    const unsigned int count = BuildMenuTap(MenuAction::Accept, buffer, sizeof(buffer));
    Require(count == 2, "a tap must be a press and a release");

    Require(ReadAt<std::uint32_t>(buffer, 0x04) == kStatePressed, "the first is pressed");
    Require(ReadAt<std::int32_t>(buffer, 0x18) == kButtonA, "accept maps to A");
    Require(ReadAt<float>(buffer, 0x20) == 1.0f, "a press carries a full value");

    // **Emitting only the press leaves the engine believing the button is held**,
    // which repeats in a menu and sticks in gameplay.
    const std::uint8_t* release = buffer + kEventSize;
    Require(ReadAt<std::uint32_t>(release, 0x04) == kStateReleased, "the second is released");
    Require(ReadAt<std::int32_t>(release, 0x18) == kButtonA, "and names the same key");
    Require(ReadAt<float>(release, 0x20) == 0.0f, "a release carries zero");

    Require(BuildMenuTap(MenuAction::Accept, buffer, kEventSize) == 0,
            "a buffer with room for one event must be refused, not half filled");
}

void TestEveryMenuActionIsMapped()
{
    const MenuAction actions[] = {
        MenuAction::Up, MenuAction::Down, MenuAction::Left, MenuAction::Right,
        MenuAction::Accept, MenuAction::Cancel, MenuAction::Start,
    };
    std::uint8_t buffer[kEventSize * 2];
    for (const MenuAction action : actions) {
        const int keyId = MenuActionKeyId(action);
        Require(keyId > 0, "every menu action must map to a key id");
        Require(KeyNameFor(keyId) != nullptr, "and that key must have a stable name");
        Require(BuildMenuTap(action, buffer, sizeof(buffer)) == 2, "and must build a tap");
    }
    Require(MenuActionKeyId(MenuAction::Up) != MenuActionKeyId(MenuAction::Down),
            "distinct actions must not collapse onto one key");
}

} // namespace

int main()
{
    TestEventLandsAtTheDocumentedOffsets();
    TestRefusalsRatherThanQuestionableEvents();
    TestKeyNamesAreStableStorage();
    TestDeviceFollowsTheKeyIdRange();
    TestInputCharIsWrittenAtOffsetEight();
    TestMenuTapIsPressThenRelease();
    TestEveryMenuActionIsMapped();
    std::cout << "input event tests passed\n";
    return 0;
}
