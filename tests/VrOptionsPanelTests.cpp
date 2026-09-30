#include "../src/dll/VrOptionsPanel.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(int argc,char** argv){
 using namespace preyvr;auto values=options::Defaults();
 // The first font raster initializes process-wide GDI caches. Measure
 // steady-state repaint ownership after warming each page.
 for(unsigned page=0;page<options::Pages;++page){
  auto pixels=dll::DrawVrOptions(values,page,0,false);
  Check(pixels.size()==options::Width*options::Height*4,"production panel dimensions");
  for(size_t n=3;n<pixels.size();n+=4)Check(pixels[n]==255,"opaque panel alpha");
  if(argc>1){
   const std::string path=std::string(argv[1])+"-"+std::to_string(page)+".bmp";
   std::ofstream out(path,std::ios::binary);
   BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
   file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
   BITMAPINFOHEADER info{sizeof(info),options::Width,-static_cast<LONG>(options::Height),1,32,BI_RGB};
   out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));
   out.write(reinterpret_cast<char*>(pixels.data()),pixels.size());Check(static_cast<bool>(out),"preview write");
  }
 }
 const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(unsigned n=0;n<16;++n)dll::DrawVrOptions(values,n%3,n%4,n%2!=0);
 const DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 std::cout<<"GDI objects before="<<before<<" after="<<after<<"\n";
 Check(after<=before+1,"repaints release GDI resources");
 std::cout<<"Production panel raster and resource checks passed\n";
}
