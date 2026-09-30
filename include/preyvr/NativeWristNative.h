#pragma once
#include <cstdint>
#include <cstddef>
namespace preyvr::hud::native {
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
 {0x18BABA0,799,0x9F87BBA79DD8FDB1ull}, // SpriteDisplay
 {0x18ABE10,236,0x834AA285258F2231ull}, // GetVariable
 {0x183BDE0,155,0x33DA6756774D21F1ull}, // ReleaseValue
 {0x183F680,654,0xB433B16833B799AAull}, // ConvertValue
 {0x1894A30,448,0x59B5592ECBE9CBB5ull}, // CharacterHandle
 {0x1894530,300,0xE3153B243DAFBA7Full}, // ConstructHandle
 {0x189E270,680,0xC2BEAEEB8B2DE361ull}, // DisplayList
 {0x18BF0C0,217,0x805D7CA10465ABEEull}, // SpriteSetVisible
 {0x18B8ED0,8,0xD06ECF8F398DB06Dull}, // SpriteOwner
};
}
