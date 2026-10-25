#include <windows.h>
#include "preyvr/SurfaceGrabTransaction.h"
#include <array>
#include <cstring>
#include <iostream>
namespace {
std::array<unsigned char,0x70> baseline{};unsigned calls=0;bool checked=false,throwing=false;
void __fastcall Consumer(std::uintptr_t self,void* request){
    ++calls;auto expected=baseline;const preyvr::Vec3 v{1.25f,-.5f,.75f};std::memcpy(expected.data()+4,&v,sizeof v);
    checked=self==0x1234&&std::memcmp(request,expected.data(),expected.size())==0;
    if(throwing)RaiseException(0xE0570001,0,0,nullptr);
}
bool Run(void* request){
    __try {preyvr::surfacegrab::InvokeMovement(Consumer,0x1234,request,{1.25f,-.5f,.75f});return true;}
    __except(GetExceptionCode()==0xE0570001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
}
int main(){
    baseline.fill(0xCD);unsigned type=4;std::memcpy(baseline.data(),&type,sizeof type);
    for(const bool fault:{false,true}){auto request=baseline;throwing=fault;calls=0;checked=false;
        const bool returned=Run(request.data());
        if(returned==fault||calls!=1||!checked||request!=baseline){std::cerr<<"movement transaction restoration failed\n";return 1;}}
    std::cout<<"surface_grab_transaction: only 12 velocity bytes changed, original once, restored on return and SEH\n";
}
