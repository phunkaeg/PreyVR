#pragma once
#include <cstddef>
#include <cstdint>
namespace preyvr::surfacegrab::native {
inline constexpr std::uintptr_t AddMovement=0x4F5D00,AnimatedVtable=0x1CDDB60,StaticVtable=0x1D82188,Caller=0x1270FAF;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x4F5D00,64,0xD4985D11A1DE1655ull}, // AddMovement entry
    {0x4F5E4D,128,0xD9D4088529FDAC59ull}, // request copy
    {0x1270EE3,7,0xA8AD7CE0589EB8DFull}, // zero-g request type
    {0x1270F94,27,0xA4D95BE19D2AE13Cull}, // zero-g request call
    {0x4F5941,21,0x2794E19648452380ull}, // animated constructor
    {0x157C6D0,8,0xA4FC63B7CFD81D34ull}, // player animated getter
    {0x904130,18,0xEFA18111DC699EDCull}, // entity parent getter
    {0x1587CA0,8,0x36AAA6E2F921CEF4ull}, // status component getter
    {0x148F9B0,96,0xE684420A5B4CED4Cull}, // native status predicate
    {0x159CB0A,7,0xDC6FF8577515C8C5ull}, // movement restriction
    {0xB7670D,69,0xCE1EFABF74157DA7ull}, // fixed physics constructor
    {0x1A5A880,6,0x31B5D0AF35A4361Full}, // fixed physics type
    {0x4FBE00,64,0xCB5541E350FCA5E1ull}, // movement preparation
    {0x4FBF5C,97,0x279AF1827C34D5AEull}, // velocity time conversion
    {0x4FC070,128,0xF8F3B5B2B7F28883ull}, // native physical movement
};
}
