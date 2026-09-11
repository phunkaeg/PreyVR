#pragma once
#include "preyvr/InputEvent.h"
#include <cstdint>

namespace preyvr::input {
// Single consumer. Scoped taps reserve adjacent press/release queue tickets.
// A rejected press must not emit an orphan release; a delivered press MUST
// release even if it closed the menu itself. Unscoped diagnostic input passes.
class MenuTapDispatch {
public:
    bool Allow(std::uint64_t scope,std::uint64_t current,bool modal,int key,unsigned state) const {
        if(!scope)return true;
        if(state==kStateReleased)return pending_ && scope==epoch_ && key==key_;
        return state==kStatePressed && modal && scope==current;
    }
    void Delivered(std::uint64_t scope,int key,unsigned state) {
        if(!scope)return;
        pending_=state==kStatePressed;epoch_=scope;key_=key;
    }
private:
    bool pending_=false;
    std::uint64_t epoch_=0;
    int key_=-1;
};
}
