#pragma once
#include <cstddef>
#include <cstdint>
namespace preyvr::scene::native {
// Steam SHA 7d6e322f...11a7. See docs/SCENE-RETICLE-HAPTICS-2026-09-29.md.
// Includes complete split-unwind query body, not just its first 143-byte entry.
inline constexpr std::uintptr_t WorldPointer=0x224D9C8,WorldVtable=0x1D82E70,Query=0xCCF250;
inline constexpr std::uintptr_t EntityPhysics=0x9042b0,PlayerEntityOffset=0x38,PhysicsSlot=0x228;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0xCCF250,5121,0xDCF31985B42A32B9ull}, // RayWorldIntersection
    {0xCCE960,1126,0x884CFBEACEFA9507ull}, // RayHeightfield
    {0xCD0E00,6300,0x94D0923D971D3F2ull}, // CheckCell
    {0xCCE000,1808,0x137A1F2F0BC7F4DCull}, // DrawRayOnGrid
    {0xCCE710,405,0x2D311245A090B6E4ull}, // CheckerConstructor
    {0x9042B0,160,0xB2F0B24F4870733Dull}, // EntityGetPhysics
    {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // PlayerSingleton
};
}
