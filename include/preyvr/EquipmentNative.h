#pragma once
#include <cstdint>
#include <cstddef>
namespace preyvr::equipment::native {
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
 {0x1274820,168,0x2A804FD9AFB560D7ull}, // Equip
 {0x12748D0,444,0x03CD03E55C5F1CEBull}, // EquipWeapon
 {0x1273EB0,211,0xB589771A7E65ED85ull}, // CanEquip
 {0x12773A0,108,0x2E7FF9A7D9C085C7ull}, // Unequip
 {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // Player
 {0x158B4D0,27,0x54643B4DBE542C75ull}, // Health
 {0x158B4F0,206,0xBC4217DDE912C07Cull}, // MaxHealth
 {0x14AE9E0,105,0x225A1EDE96B4EDC5ull}, // Stat
 {0x148F000,45,0x61BE71028CF1C49Cull}, // Status
 {0x10B1F40,89,0x9BF31458A5E2B913ull}, // Property
 {0x15ACFE0,58,0x5232F3F4D7ACC313ull}, // Psi
 {0x15ACFA0,58,0xB3BAFD8CA8FA5EEBull}, // MaxPsi
 {0x13F60C0,8,0x70889D7E953E56AEull}, // HealthExtension
 {0x13F60D0,165,0xE502581913377198ull}, // HealthExtensionById
 {0x13611D0,510,0x61D8F2FC910BABF6ull}, // ArmorHud
 {0x1584720,349,0x0D140C6C08E0EE48ull}, // ArmorOwner
 {0x15E8060,136,0xAAD9DF42B7C15A88ull}, // PsiOwner
 {0x158FC00,92,0xCD5D765E9D9E54CFull}, // EquipOwner
 {0x1590DD0,504,0x6696639DF296F76Aull}, // UnequipOwner
 {0x10839F0,4,0xCBB562DA69740364ull}, // PsiAccessor
 {0x1587CA0,8,0x36AAA6E2F921CEF4ull}, // StatusAccessor
 {0x16A5650,72,0xAAA04A7A0DEBB0F5ull}, // WeaponLookup
};
}
