#include "UiGuide.h"
#include <windows.h>
#include <cstring>
namespace preyvr::dll {
std::vector<std::uint8_t> MakeMenuGuide() {
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc) return {};
    BITMAPINFO info{};
    info.bmiHeader={sizeof(BITMAPINFOHEADER),kGuideWidth,-static_cast<LONG>(kGuideHeight),1,32,BI_RGB};
    void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap || !pixels) {if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return {};}
    const auto oldBitmap=SelectObject(dc,bitmap);
    RECT area{0,0,kGuideWidth,kGuideHeight};
    const auto brush=CreateSolidBrush(RGB(17,23,32));
    FillRect(dc,&area,brush);DeleteObject(brush);
    SetBkMode(dc,TRANSPARENT);
    const auto font=CreateFontW(-30,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const auto oldFont=SelectObject(dc,font);
    SetTextColor(dc,RGB(143,218,248));
    const wchar_t* title=L"PREY VR  /  MENU CONTROLS";
    TextOutW(dc,30,13,title,lstrlenW(title));
    SetTextColor(dc,RGB(240,244,248));
    const wchar_t* line1=L"Point + trigger: click / drag   A: select   B: back";
    const wchar_t* line2=L"Stick: navigate   Grips: tabs   Grips + Y: reset view";
    TextOutW(dc,30,57,line1,lstrlenW(line1));
    TextOutW(dc,30,103,line2,lstrlenW(line2));
    GdiFlush();
    std::vector<std::uint8_t> result(kGuideWidth*kGuideHeight*4);
    std::memcpy(result.data(),pixels,result.size());
    // A rectangular instruction card has no transparent content. Keep the GDI
    // sRGB bytes intact and submit it as opaque, like the native menu panel.
    for(std::size_t i=3;i<result.size();i+=4)result[i]=255;
    SelectObject(dc,oldFont);DeleteObject(font);
    SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
    return result;
}
}
