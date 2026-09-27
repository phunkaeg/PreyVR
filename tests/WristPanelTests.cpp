#include "../src/dll/WristPanel.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(int argc,char** argv){
 using namespace preyvr::dll;
 auto p=DrawWristPanel({76,100,55,100,82});
 Check(p.size()==WristWidth*WristHeight*4,"production pixel extent");
 for(size_t n=3;n<p.size();n+=4)Check(p[n]==255,"opaque pixels");
 const auto zero=DrawWristPanel({0,100,0,100,0}),unknown=DrawWristPanel({});
 Check(zero!=unknown&&zero!=p,"unknown and genuine zero have distinct displays");
 const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(int n=0;n<20;++n)DrawWristPanel({n,100,n,100,n});
 Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=before+1,"GDI resources released");
 if(argc>1){
  BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
  file.bfSize=file.bfOffBits+static_cast<DWORD>(p.size());
  BITMAPINFOHEADER info{sizeof(info),WristWidth,-static_cast<LONG>(WristHeight),1,32,BI_RGB};
  std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));
  out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(p.data()),p.size());
  Check(static_cast<bool>(out),"preview saved");
 }
 std::cout<<"Wrist production raster, unknown/zero values and GDI lifecycle passed\n";
}
