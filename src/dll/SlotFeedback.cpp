#include "SlotFeedback.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "PsiMedkit.h"
#include "VrOptionsRuntime.h"
#include <windows.h>
#include <cstring>
namespace preyvr::dll {
namespace { LatestSnapshot<equipment::SlotMessage> feedback; }
void PublishSlotFeedback(equipment::SlotNotice kind,const GameplayPoseFrame& f){
    if(!BeltHintsEnabled())return;
    feedback.Publish({kind,f.tracking.epoch,f.referenceGeneration,HudMenuEpoch(),MonotonicNanoseconds()});
}
bool ReadSlotFeedback(const TrackingFrame& f,equipment::SlotMessage& out){
    if(!BeltHintsEnabled()||!HudGameplayInputAllowed()||
       !IsPoseUsable(f.head,f.headValidity,200000000ull)||
       !FreshSample(MonotonicNanoseconds(),f.publishedNs)||!feedback.TryRead(out)||
       !out.Current(MonotonicNanoseconds(),f.epoch,HeadTrackingReferenceGeneration(),HudMenuEpoch()))return false;
    const bool medkit=out.kind>=equipment::SlotNotice::MedkitReady&&out.kind<=equipment::SlotNotice::MedkitDenied;
    return medkit?(MedkitSlotEnabled()&&(out.kind!=equipment::SlotNotice::MedkitReady||MedkitOwnsGrip())):HolstersEnabled();
}
void ClearSlotFeedback(){feedback.Clear();}
std::vector<std::uint8_t> DrawSlotFeedback(equipment::SlotNotice kind){
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return {};
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),SlotFeedbackWidth,-static_cast<LONG>(SlotFeedbackHeight),1,32,BI_RGB};
    void* pixels=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return {};}
    const auto previous=SelectObject(dc,bitmap);
    RECT bounds{0,0,SlotFeedbackWidth,SlotFeedbackHeight};
    auto brush=CreateSolidBrush(RGB(12,18,24));FillRect(dc,&bounds,brush);DeleteObject(brush);
    bounds={0,0,8,SlotFeedbackHeight};brush=CreateSolidBrush(RGB(241,168,63));FillRect(dc,&bounds,brush);DeleteObject(brush);
    SetBkMode(dc,TRANSPARENT);
    auto text=[&](std::wstring_view s,int y,int size,COLORREF color){
        auto font=CreateFontW(-size,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Bahnschrift");
        auto old=SelectObject(dc,font);SetTextColor(dc,color);RECT r{40,y,990,y+85};
        DrawTextW(dc,s.data(),static_cast<int>(s.size()),&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);DeleteObject(font);
    };
    text(equipment::SlotTitle(kind),36,43,RGB(241,168,63));
    text(equipment::SlotDetail(kind),108,37,RGB(232,230,213));
    GdiFlush();std::vector<std::uint8_t> out(SlotFeedbackWidth*SlotFeedbackHeight*4);std::memcpy(out.data(),pixels,out.size());
    for(std::size_t n=3;n<out.size();n+=4)out[n]=255;
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return out;
}
}
