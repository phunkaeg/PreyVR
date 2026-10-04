#include "../src/dll/SlotFeedback.cpp"
#include <cstdlib>
#include <iostream>
#include <fstream>
namespace {
using namespace preyvr;
bool hints=true,gameplay=true,medkit=true,grip=true,holsters=true;
std::uint64_t reference=2,menu=5;
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
}
namespace preyvr::dll {
bool BeltHintsEnabled(){return hints;}
bool HudGameplayInputAllowed(){return gameplay;}
unsigned long long HudMenuEpoch(){return menu;}
unsigned long long HeadTrackingReferenceGeneration(){return reference;}
unsigned MedkitSlotEnabled(){return medkit;}
bool MedkitOwnsGrip(){return grip;}
bool HolstersEnabled(){return holsters;}
}
int main(int argc,char** argv){
 using namespace preyvr;using namespace preyvr::dll;using N=equipment::SlotNotice;
 GameplayPoseFrame f{};f.referenceGeneration=reference;f.tracking.epoch=7;f.tracking.publishedNs=MonotonicNanoseconds();
 f.tracking.headValidity={true,true,true,true,0};
 PublishSlotFeedback(N::MedkitReady,f);equipment::SlotMessage message{};
 Check(ReadSlotFeedback(f.tracking,message)&&message.kind==N::MedkitReady,"selected medkit visible");
 grip=false;Check(!ReadSlotFeedback(f.tracking,message),"release hides an unused selected medkit");
 PublishSlotFeedback(N::MedkitUsed,f);Check(ReadSlotFeedback(f.tracking,message),"confirmed use survives release briefly");
 Check(!message.Current(message.stamp+1500000001ull,7,2,5),"feedback expires");
 Check(!message.Current(message.stamp-1,7,2,5),"clock inversion refuses");
 Check(!message.Current(message.stamp,8,2,5)&&!message.Current(message.stamp,7,4,5)&&!message.Current(message.stamp,7,3,5)&&!message.Current(message.stamp,7,2,6),"session, reference, changing reference and modal transitions hide old feedback");
 medkit=false;Check(!ReadSlotFeedback(f.tracking,message),"feature disabled hides old medkit feedback");
 PublishSlotFeedback(N::HipStored,f);Check(ReadSlotFeedback(f.tracking,message),"holster feedback independent of medkit");
 f.tracking.headValidity.orientationValid=false;Check(!ReadSlotFeedback(f.tracking,message),"tracking loss hides an otherwise fresh toast");
 f.tracking.headValidity.orientationValid=true;
 holsters=false;Check(!ReadSlotFeedback(f.tracking,message),"disabled holsters hide feedback");holsters=true;
 gameplay=false;Check(!ReadSlotFeedback(f.tracking,message),"no feedback on game menu");gameplay=true;
 hints=false;Check(!ReadSlotFeedback(f.tracking,message),"hints opt out");hints=true;
 ClearSlotFeedback();Check(!ReadSlotFeedback(f.tracking,message),"teardown clears feedback");
 std::array<ui::Eye,2> eyes{};Pose head{{},{0,1.65f,0}};
 for(unsigned e=0;e<2;++e)eyes[e]={Pose{{},{e==0?-.0315f:.0315f,1.65f,0}},e==0?-.942478f:-.767945f,e==0?.767945f:.942478f,.959931f,-.959931f};
 auto panel=equipment::SlotPanel(head,eyes);
 Check(panel&&ui::CornersVisible(*panel,eyes,.65f)&&panel->width>.4f,"card fits Quest 3 central safe framing");
 eyes[0].down=0;Check(!equipment::SlotPanel(head,eyes),"invalid optical geometry never produces a card");
 DWORD before=0;
 for(int pass=0;pass<3;++pass){
  if(pass==1)before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
  for(unsigned n=1;n<=static_cast<unsigned>(N::ChestDenied);++n){
   auto pixels=DrawSlotFeedback(static_cast<N>(n));
   Check(pixels.size()==SlotFeedbackWidth*SlotFeedbackHeight*4,"production card raster dimensions");
   for(std::size_t p=3;p<pixels.size();p+=4)Check(pixels[p]==255,"card alpha opaque");
   if(argc>1&&pass==0&&n==1){
    std::ofstream out(argv[1],std::ios::binary);BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{sizeof(info),SlotFeedbackWidth,-static_cast<LONG>(SlotFeedbackHeight),1,32,BI_RGB};
    out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(pixels.data()),pixels.size());Check(bool(out),"preview written");
   }
  }
 }
 Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=before+1,"all card paints release GDI objects");
 std::cout<<"Slot feedback lifetime, native feature isolation, optical fit and production raster passed\n";
}
