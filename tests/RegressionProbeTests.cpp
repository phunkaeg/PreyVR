// Actual diagnostic serializer; all publications replaced, no game memory.
#include "../src/dll/RegressionProbe.cpp"
#include <iostream>
#include <stdexcept>
namespace {bool fresh=true;unsigned long long generation=2;float cameraZ=3;void Check(bool b,const char* s){if(!b)throw std::runtime_error(s);}}
namespace preyvr::dll {
bool TryGetTrackingFrame(TrackingFrame& t){t={};t.publishedNs=fresh?MonotonicNanoseconds()-1000000:1;t.epoch=3;t.sequence=7;
 t.headValidity=t.hands[0].gripValidity=t.hands[1].gripValidity={true,true,true,true,0};return true;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& p,bool){p={};p.publishedNs=MonotonicNanoseconds()-1000000;p.referenceGeneration=2;p.tracking.epoch=3;
 p.cameraCentreValid=true;p.cameraCentre={1,2,cameraZ};p.weaponGeneration=4;return true;}
bool TryGetSupportGripTargetXr(Vec3& v){v={.1f,1.3f,-.4f};return true;}
unsigned long long HeadTrackingReferenceGeneration(){return generation;}
bool HudMenuStateKnown(){return fresh;}unsigned long long HudVisibleModalMask(){return 16;}
unsigned long long HudMenuEpoch(){return 5;}unsigned long long HudMenuStateSampleCount(){return 20;}
bool HudInventoryIsOpen(){return fresh;}bool HudGameplayInputAllowed(){return false;}bool VrOptionsInputOwned(){return false;}
bool MenuNavigationEnabled(){return true;}unsigned long long MenuNavigationActionCount(){return 2;}
bool TwoHandedAimHeld(){return false;}bool SupportGripNear(){return true;}unsigned TwoHandedAimEnabled(){return 1;}
bool TwoHandGripToggle(){return false;}
bool NativeFlightSessionActive(){return true;}int NativeFlightObservedState(){return 9;}DWORD NativeFlightObservedOwnerError(){return 0;}
unsigned NativeFlightControlsEnabled(){return 1;}unsigned SurfaceGrabEnabled(){return 0;}
unsigned SurfaceGrabReach(){return 10;}unsigned SurfacePushStrength(){return 100;}unsigned SurfacePushLimit(){return 150;}
SurfaceGrabCounters ReadSurfaceGrabCounters(){return {2,1,30,1,0,false,false,0,42};}
unsigned long long InventoryLayerFrameCount(){return 100;}bool InventoryCaptureEnabled(){return true;}
const char* XrTrackingSpaceName(){return "STAGE";}
unsigned BodyYawFollowEnabled(){return 1;}unsigned BodyRoomScaleEnabled(){return 0;}
}
int main(int argc,char** argv){try{
 using namespace preyvr::dll;
 const auto first=RegressionStateJson("fixture_1");Check(first.find("\"request\":\"fixture_1\"")!=std::string::npos,"request echoed exactly");
 Check(first.find("\"pose\":{\"valid\":true")!=std::string::npos,"positive-control pose accepted");
 cameraZ=NAN;const auto bad=RegressionStateJson("fixture_nan");Check(bad.find("nan,")==std::string::npos&&bad.find("[1,2,null]")!=std::string::npos,"nonfinite data emits JSON null");
 Check(bad.find("\"pose\":{\"valid\":false")!=std::string::npos,"nonfinite camera cannot authenticate pose");cameraZ=3;
 Check(first.find("\"captures\":2")!=std::string::npos&&first.find("\"mask\":16")!=std::string::npos,"actual observed counters and modal mask preserved");
 Check(first.find("\"releaseSerial\":42")!=std::string::npos,"delivered release receipt remains distinct from held submissions");
 fresh=false;const auto stale=RegressionStateJson("fixture_2");Check(stale.find("\"tracking\":{\"valid\":false")!=std::string::npos,"stale tracking explicitly invalid");
 Check(stale.find("\"pose\":{\"valid\":false")!=std::string::npos,"stale tracking cannot authenticate gameplay pose");
 Check(RegressionStateJson("bad\ncommand").find("\"result\":87")!=std::string::npos,"injected/malformed request denied");
 Check(RegressionStateJson("").find("\"result\":87")!=std::string::npos,"missing token denied");
 if(argc>1&&std::string(argv[1])=="--json")std::cout<<first;else std::cout<<"regression_probe: correlation, JSON values and stale publication gates passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
