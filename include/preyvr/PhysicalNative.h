#pragma once
#include <cstddef>
#include <cstdint>
namespace preyvr::physical::native {
inline constexpr std::uintptr_t WrenchVtable=0x1E92F00,Attack=0x16B1FD0,WeaponHit=0x16B2BC0,GetHits=0x13BD620,AppendHit=0x13BD250,WeaponImpulse=0x16AE410;
inline constexpr std::uintptr_t RigidVtable=0x1D83870,RigidAction=0xBCF990,RigidGetType=0x13D1B90,CharacterAabb=0x82DD40;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x16B1FD0,648,0x2EE831A4AAD157F1ull}, // Attack
    {0x16B2BC0,601,0x8A71195CD45B9345ull}, // WeaponHit
    {0x13BF730,2226,0x4DB3B8443E6B0F09ull}, // ComponentHit
    {0x13BD620,3600,0xE28A525A5F2816B7ull}, // GetHits
    {0x13BD250,560,0xE7569E6878672502ull}, // AppendHit
    {0x892550,125,0x678FECB78D3897E2ull}, // VectorAllocate
    {0x13C01A0,202,0x296C28B084A8360Aull}, // VectorCommit
    {0x16AE410,211,0x77FF6F3CE46B7448ull}, // WeaponImpulse
    {0x1231940,560,0xF77A755A0D82954Full}, // ImpulseProducer
    {0xB1B160,1355,0xBB629C1F42F06802ull}, // QueueAction
    {0x1692E40,21,0x724854A717C263FAull}, // AttackPrelude
    {0xBCF990,13784,0xEDC8A7EDEA03ABEDull}, // RigidAction
    {0xB43870,1093,0xCF96EA2E1F8A0CAFull}, // ImpulseValidation
    {0x13D1B90,6,0x2505EF84B8531C22ull}, // RigidGetType
    {0x82DD40,8,0xFC342393E2A68B8Bull}, // CharacterAabb
};
}
