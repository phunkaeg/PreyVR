#pragma once
#include "preyvr/VrMath.h"
#include <cstdint>
#include <cstring>
// Windows native seam: patch only the three velocity floats in the caller's
// request. Preserve the rest, invoke the existing consumer once, restore on SEH
// as well as normal completion. Kept separate so the actual transaction can be
// fixture-tested without calling game memory or loading a hook.
namespace preyvr::surfacegrab {
using MovementCall=void(__fastcall*)(std::uintptr_t,void*);
inline void InvokeMovement(MovementCall call,std::uintptr_t self,void* request,Vec3 velocity){
    Vec3 saved{};auto* field=static_cast<unsigned char*>(request)+4;
    std::memcpy(&saved,field,sizeof saved);
    __try {std::memcpy(field,&velocity,sizeof velocity);call(self,request);}
    __finally {std::memcpy(field,&saved,sizeof saved);}
}
}
