#pragma once
#include <cstdint>
#include <string_view>
#include "preyvr/LatestSnapshot.h"
#include "preyvr/UiPanel.h"
namespace preyvr::equipment {
enum class SlotNotice { None,MedkitReady,MedkitUsed,MedkitEmpty,MedkitDenied,
    HipStored,ChestStored,HipDraw,ChestDraw,HipDenied,ChestDenied };
struct SlotMessage {
    SlotNotice kind=SlotNotice::None;
    std::uint64_t epoch=0,reference=0,menu=0,stamp=0;
    bool Current(std::uint64_t now,std::uint64_t e,std::uint64_t r,std::uint64_t m)const {
        return kind!=SlotNotice::None&&e==epoch&&r==reference&&!(r&1)&&m==menu&&
            FreshSample(now,stamp,1500000000ull);
    }
};
inline std::wstring_view SlotTitle(SlotNotice n){
    switch(n){
    case SlotNotice::MedkitReady:case SlotNotice::MedkitUsed:case SlotNotice::MedkitEmpty:case SlotNotice::MedkitDenied:return L"LEFT HIP / MEDKIT";
    case SlotNotice::HipStored:case SlotNotice::HipDraw:case SlotNotice::HipDenied:return L"RIGHT HIP / WEAPON";
    case SlotNotice::ChestStored:case SlotNotice::ChestDraw:case SlotNotice::ChestDenied:return L"LEFT CHEST / WEAPON";
    default:return L"";
    }
}
inline std::wstring_view SlotDetail(SlotNotice n){
    switch(n){
    case SlotNotice::MedkitReady:return L"HOLD GRIP + PRESS LEFT TRIGGER TO USE";
    case SlotNotice::MedkitUsed:return L"MEDKIT USED";
    case SlotNotice::MedkitEmpty:return L"NO MEDKITS IN INVENTORY";
    case SlotNotice::MedkitDenied:return L"MEDKIT USE UNAVAILABLE";
    case SlotNotice::HipStored:case SlotNotice::ChestStored:return L"WEAPON ASSIGNED / STOW REQUESTED";
    case SlotNotice::HipDraw:case SlotNotice::ChestDraw:return L"DRAW REQUEST ACCEPTED";
    case SlotNotice::HipDenied:case SlotNotice::ChestDenied:return L"HOLSTER ACTION UNAVAILABLE";
    default:return L"";
    }
}
// A small transient card below the central view, checked against both runtime
// eye frusta with a conservative lens margin. Hidden if it cannot safely fit.
inline std::optional<ui::Panel> SlotPanel(const Pose& head,const std::array<ui::Eye,2>& eyes){
    ui::Panel panel{Compose(head,Pose{{},{0,-.28f,-1.2f}}),.6f,.6f*224.f/1024.f};
    for(int n=0;n<10;++n){
        if(ui::CornersVisible(panel,eyes,.65f))return panel;
        panel.width*=.9f;panel.height*=.9f;
    }
    return {};
}
}
