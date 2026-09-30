#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include "preyvr/NativeWristNative.h"
namespace preyvr::dll {
inline bool VerifyNativeWrist(std::uintptr_t base){
    __try {
        for(const auto& entry:hud::native::Contracts){
            std::uint64_t hash=14695981039346656037ull;
            auto bytes=reinterpret_cast<const unsigned char*>(base+entry.rva);
            for(std::size_t i=0;i<entry.size;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=entry.hash)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Steam converters 183F680/183F4F0 and retain/release 183BD70/183BDE0.
struct NativeHudValue {
    void* interfacePointer=nullptr;
    std::uint32_t type=0,padding=0;
    std::uint64_t payload=0;
};
static_assert(sizeof(NativeHudValue)==24 && offsetof(NativeHudValue,type)==8 &&
              offsetof(NativeHudValue,payload)==16);
struct NativeHudApi {
    using Value=NativeHudValue;
    void* root;
    std::uintptr_t base;
    using Get=bool(__fastcall*)(void*,Value*,const char*);
    using Drop=void(__fastcall*)(void*,Value*,std::uintptr_t);
    bool Valid() const {
        __try {
            auto v=*reinterpret_cast<std::uintptr_t**>(root);
            return reinterpret_cast<std::uintptr_t>(v)==base+0x1EB4B60 &&
                v[0x88/8]==base+0x18ABE10;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Resolve(const char* path,Value& value,void*& object){
        __try {
            if(!reinterpret_cast<Get>(base+0x18ABE10)(root,&value,path)||value.type!=0x48||!value.payload)return false;
            // 183F680 -> 1894A30 -> 1894530 constructs a character handle;
            // handle+8 is the live whole GFxASCharacter pointer. Never fall back
            // to name resolution for a detached handle or retain it across frames.
            object=*reinterpret_cast<void**>(value.payload+8);
            if(!object)return false;
            auto v=*reinterpret_cast<std::uintptr_t**>(object);
            return reinterpret_cast<std::uintptr_t>(v)==base+0x1EB5700 &&
                v[0xE8/8]==base+0x18BABA0 &&
                *reinterpret_cast<void**>(static_cast<char*>(object)+0x118)==root;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Release(Value& value){
        // Relinquish our ownership before entering native release. If it faults
        // after decrementing a reference, retrying could double-release it.
        auto owned=value;value={};
        __try {
            if(owned.type&0x40)reinterpret_cast<Drop>(base+0x183BDE0)(owned.interfacePointer,&owned,owned.payload);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
