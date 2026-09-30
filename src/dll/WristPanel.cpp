#include "WristPanel.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <algorithm>
namespace preyvr::dll {
std::vector<std::uint8_t> DrawWristPanel(const equipment::Vitals& v){
 HDC dc=CreateCompatibleDC(nullptr);if(!dc)return {};
 BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),WristWidth,-static_cast<LONG>(WristHeight),1,32,BI_RGB};
 void* pixels=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
 if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return {};}
 auto oldBitmap=SelectObject(dc,bitmap);
 auto fill=[&](int x,int y,int w,int h,COLORREF c){RECT r{x,y,x+w,y+h};auto brush=CreateSolidBrush(c);FillRect(dc,&r,brush);DeleteObject(brush);};
 auto text=[&](int x,int y,int w,const std::wstring& s,int size,COLORREF c){
  auto font=CreateFontW(-size,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Bahnschrift");
  auto old=SelectObject(dc,font);SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);
  RECT r{x,y,x+w,y+64};DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
  SelectObject(dc,old);DeleteObject(font);
 };
 const auto bg=RGB(12,18,24),ivory=RGB(232,230,213),amber=RGB(241,168,63);
 fill(0,0,WristWidth,WristHeight,bg);fill(0,0,WristWidth,6,amber);
 text(28,22,670,L"TRANSTAR / PERSONAL STATUS",25,amber);
 const int current[]={v.health,v.psi,v.suit},maximum[]={v.maxHealth,v.maxPsi,100};
 const wchar_t* names[]={L"HEALTH",L"PSI",L"SUIT"};
 for(int row=0;row<3;++row){
  const int y=83+row*98;const bool valid=current[row]>=0&&maximum[row]>0;
  const auto color=valid&&current[row]*4<maximum[row]?RGB(244,112,80):ivory;
  text(28,y,250,names[row],36,color);
  auto number=valid?std::to_wstring(current[row]):L"--";
  if(valid)number+=row==2?L" %":L" / "+std::to_wstring(maximum[row]);
  text(290,y,410,number,42,color);
  fill(30,y+57,658,9,RGB(44,57,65));
  if(valid)fill(30,y+57,static_cast<int>(658.f*std::clamp(static_cast<float>(current[row])/maximum[row],0.f,1.f)),9,color);
 }
 GdiFlush();std::vector<std::uint8_t> out(WristWidth*WristHeight*4);std::memcpy(out.data(),pixels,out.size());
 for(std::size_t i=3;i<out.size();i+=4)out[i]=255;
 SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);return out;
}
}
