#include "VrOptionsPanel.h"
#include <windows.h>
#include <cstring>
namespace preyvr::dll {
std::vector<std::uint8_t> DrawVrOptions(const options::Values& values,unsigned page,unsigned selected,bool saveFailed){
 using namespace options;
 HDC dc=CreateCompatibleDC(nullptr);if(!dc)return {};
 BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),Width,-static_cast<LONG>(Height),1,32,BI_RGB};
 void* pixels=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
 if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return {};}
 const auto oldBitmap=SelectObject(dc,bitmap);
 auto fill=[&](int x,int y,int w,int h,COLORREF c){RECT r{x,y,x+w,y+h};auto b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);};
 auto text=[&](int x,int y,int w,int h,const std::wstring& s,int size,COLORREF c,bool bold=false){
  auto font=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Bahnschrift");
  const auto old=SelectObject(dc,font);SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);
  RECT r{x,y,x+w,y+h};DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
  SelectObject(dc,old);DeleteObject(font);
 };
 const auto bg=RGB(12,18,24),ink=RGB(232,230,213),muted=RGB(155,169,174),amber=RGB(241,168,63);
 fill(0,0,Width,Height,bg);
 fill(0,0,Width,8,amber);fill(55,53,10,83,amber);
 text(86,46,950,28,L"T R A N S T A R   /   PERSONAL SYSTEMS",25,muted);
 text(83,78,1070,75,L"VIRTUAL REALITY",55,ink,true);
 text(1120,70,230,80,L"CONFIGURATION\n01 / TALOS I",21,muted);
 for(unsigned p=0;p<Pages;++p){
  const int x=56+static_cast<int>(p)*429;
  fill(x,174,421,72,p==page?amber:RGB(27,37,45));
  text(x+26,193,375,45,PageName(p),30,p==page?bg:muted,true);
 }
 for(unsigned r=0;r<Rows;++r){
  const int y=280+static_cast<int>(r)*116;const bool active=r==selected;
  fill(56,y,1288,108,active?RGB(47,48,40):RGB(22,30,37));
  if(active)fill(56,y,6,108,amber);
  const int id=Item(page,r);
  text(83,y+24,710,65,id<0?L"RESET VR VIEW":Describe(id).label,32,active?amber:ink,true);
  const auto value=id<0?L"RECENTER":Display(id,values[id]);
  text(813,y+27,44,55,L"<",30,muted);
  text(874,y+27,382,63,value,29,ink,true);
  text(1290,y+27,38,55,L">",30,muted);
 }
 fill(56,778,1288,2,RGB(69,80,83));
 const int selectedId=Item(page,selected);
 text(80,808,1240,103,selectedId<0?
      L"Look straight ahead and select to reset the view and controller reference together.":
      Describe(selectedId).help,29,ink);
 text(80,914,1250,52,saveFailed?L"SETTINGS ACTIVE / COULD NOT SAVE TO DISK":
      L"POINT + TRIGGER   /   STICK: SELECT & ADJUST   /   GRIPS: TABS",24,saveFailed?amber:muted);
 fill(56,980,1288,80,RGB(36,47,54));
 text(85,1001,1150,54,L"B  /  BACK TO GAME MENU",29,ink,true);
 GdiFlush();std::vector<std::uint8_t> result(Width*Height*4);
 std::memcpy(result.data(),pixels,result.size());
 for(std::size_t i=3;i<result.size();i+=4)result[i]=255;
 SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);return result;
}
}
