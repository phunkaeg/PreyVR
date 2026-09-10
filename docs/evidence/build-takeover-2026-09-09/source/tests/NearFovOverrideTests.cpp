// Exercise the production detour and SEH adapter without loading/injecting Prey.
// Including this TU exposes internal seams only inside this test executable.
#include "../src/dll/NearFovOverride.cpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
DWORD ModulePinStatus() { return 0; }
bool EnsureMinHook() { return false; }
}

namespace {
using namespace preyvr::dll;
float nativeFov = 55.0f;
unsigned originalCalls = 0;
void Require(bool value, const char* message)
{
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
void __fastcall EmulateLatch(void* renderer)
{
    ++originalCalls;
    if (renderer != nullptr) {
        std::memcpy(static_cast<std::uint8_t*>(renderer) + kDrawNearFovLatched,
                    &nativeFov, sizeof(nativeFov));
    }
}
struct Fixture {
    std::vector<std::uintptr_t> table = std::vector<std::uintptr_t>(0x900 / 8);
    std::vector<std::uint8_t> renderer = std::vector<std::uint8_t>(0x9600, 0xCD);
    Fixture()
    {
        table[kBeginFrameSlot / 8] = reinterpret_cast<std::uintptr_t>(&EmulateLatch);
        const auto* vtable = table.data();
        std::memcpy(renderer.data(), &vtable, sizeof(vtable));
        gTarget = reinterpret_cast<void*>(&EmulateLatch);
        gOriginal.store(&EmulateLatch);
        gFaulted.store(false);
        gObservedValid.store(false);
        gApplied.store(0);
        gRefused.store(0);
        gDeciDegrees.store(885);
        originalCalls = 0;
        nativeFov = 55.0f;
    }
    float Field() const
    {
        float result = 0;
        std::memcpy(&result, renderer.data() + kDrawNearFovLatched, sizeof(result));
        return result;
    }
    void Frame() { BeginFrameWithNearFov(renderer.data()); }
};
void TestLatchAndDisable()
{
    Fixture f;
    const auto before = f.renderer;
    f.Frame();
    Require(originalCalls == 1 && f.Field() == 88.5f, "override follows original latch exactly once");
    Require(NearFovObservedValid() && NearFovObservedDeciDegrees() == 550,
            "observation is the native latch, not the override");
    Require(NearFovThreadId() == GetCurrentThreadId(), "write stays on callback thread");
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (i < kDrawNearFovLatched || i >= kDrawNearFovLatched + sizeof(float)) {
            Require(f.renderer[i] == before[i], "no neighboring renderer bytes changed");
        }
    }
    nativeFov = 70.0f; // model a cvar reset/zoom change between frames
    f.Frame();
    Require(f.Field() == 88.5f && NearFovObservedDeciDegrees() == 700,
            "next native latch cannot erase the override");
    Require(SetNearFovDeciDegrees(0) == 0, "disable accepted without installing anything");
    f.Frame();
    Require(f.Field() == 70.0f && originalCalls == 3 && NearFovAppliedCount() == 2,
            "disable exposes fresh engine cvar at next begin, not an old saved value");
}
void TestSentinelsAndFaults()
{
    Fixture f;
    for (const float sentinel : {0.0f, -1.0f}) {
        nativeFov = sentinel;
        f.Frame();
        Require(f.Field() == 88.5f && NearFovObservedDeciDegrees() == static_cast<int>(sentinel * 10),
                "engine fallback sentinel does not prevent an override");
    }
    f.table[kBeginFrameSlot / 8] = 0;
    nativeFov = 55.0f;
    f.Frame();
    Require(f.Field() == 55.0f && NearFovFaulted() && !NearFovObservedValid(),
            "wrong receiver slot refuses write and disarms");
    const auto refused = NearFovRefusedCount();
    f.table[kBeginFrameSlot / 8] = reinterpret_cast<std::uintptr_t>(&EmulateLatch);
    f.Frame();
    Require(f.Field() == 55.0f && NearFovRefusedCount() == refused,
            "fault does not create a retry loop; original keeps running");
    int observed = 0;
    Require(!ApplyToRenderer(nullptr, 885, &observed), "null renderer refused");
    Require(!ApplyToRenderer(reinterpret_cast<void*>(1), 885, &observed), "unreadable renderer caught by SEH");
    gFaulted.store(false);
    nativeFov = std::numeric_limits<float>::quiet_NaN();
    f.Frame();
    Require(NearFovFaulted() && std::isnan(f.Field()), "nonfinite native value stays unwritten");
    gFaulted.store(false);
    gOriginal.store(nullptr);
    const auto count = originalCalls;
    f.Frame();
    Require(originalCalls == count && std::isnan(f.Field()), "no original means no mutation");
}
void TestGates()
{
    Require(MatchesCode(kBeginFramePrologue.data(), kLatchInstructions.data()), "known gate bytes match");
    auto latch = kLatchInstructions;
    latch[20] ^= 1; // renderer field displacement
    Require(!MatchesCode(kBeginFramePrologue.data(), latch.data()), "wrong member offset rejected");
    latch = kLatchInstructions;
    latch[4] ^= 1; // cvar source displacement
    Require(!MatchesCode(kBeginFramePrologue.data(), latch.data()), "wrong cvar source rejected");
    auto entry = kBeginFramePrologue;
    entry[16] ^= 1; // beyond the old ambiguous 16-byte prologue
    Require(!MatchesCode(entry.data(), kLatchInstructions.data()), "prologue collision rejected");
    Require(!MatchesCode(nullptr, nullptr), "unreadable code rejected");
    gDeciDegrees.store(885);
    for (const unsigned value : {1u, 10u, 1790u, 1800u, 0xFFFFFFFFu}) {
        Require(SetNearFovDeciDegrees(value) != 0 && NearFovDeciDegrees() == 885,
                "ineffective/out-of-range setting preserves active request");
    }
    gInstalled = false;
    Require(SetNearFovDeciDegrees(900) == 2 && NearFovDeciDegrees() == 885,
            "unavailable installer preserves request");
    // Simulate successful hook installation solely for command boundary tests.
    gInstalled = true;
    Require(SetNearFovDeciDegrees(11) == 0 && NearFovDeciDegrees() == 11, "lower bound");
    Require(SetNearFovDeciDegrees(1789) == 0 && NearFovDeciDegrees() == 1789, "upper bound");
    gFaulted.store(true);
    Require(SetNearFovDeciDegrees(885) == 0 && !NearFovFaulted(), "explicit command rearms");
    gInstalled = false;
}
}
int main()
{
    TestLatchAndDisable();
    TestSentinelsAndFaults();
    TestGates();
    std::cout << "near_fov_override: production callback, native latch, guards and disable passed\n";
}
