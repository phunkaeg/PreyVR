// Exercise the production guarded receiver adapter without a game process.
#include "../src/dll/InventoryPointerCapture.cpp"
#include <cstdlib>
#include <iostream>
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace {DWORD pinned=1;}
namespace preyvr::dll {DWORD ModulePinStatus(){return pinned;}bool EnsureMinHook(){return false;}bool HudMenuIsOpen(){return true;}}
namespace {
using namespace preyvr::dll;
unsigned calls=0;
void* seen=nullptr;
bool __fastcall NativeCancel(void* self){++calls;seen=self;return false;}
void Require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
struct Fixture {
    alignas(8) std::array<unsigned char,0x80> inventory{},sender{};
    std::uintptr_t base=reinterpret_cast<std::uintptr_t>(&NativeCancel)-kCancel;
    Capture capture{reinterpret_cast<std::uintptr_t>(inventory.data()),reinterpret_cast<std::uintptr_t>(sender.data()),42,GetCurrentThreadId()};
    Fixture(){
        *reinterpret_cast<std::uintptr_t*>(inventory.data())=base+kInventoryTable;
        *reinterpret_cast<std::uintptr_t*>(sender.data())=base+0x1CAB358;
        *reinterpret_cast<unsigned*>(inventory.data()+0x28)=42;
        inventory[0x34]=inventory[0x35]=sender[0x70]=1;calls=0;seen=nullptr;
    }
    DWORD Cancel(bool& cancelled){cancelled=false;return CancelChecked(base,capture,&cancelled);}
};
}
int main(){
    pinned=0;Require(EnsureInventoryPointerCapture()==ERROR_INVALID_STATE,"unloadable module refuses hooks");
    pinned=1;Require(EnsureInventoryPointerCapture()==ERROR_BAD_EXE_FORMAT,"pinned module proceeds to target identity guard");
    Fixture f;bool cancelled=false;
    Require(f.Cancel(cancelled)==0&&cancelled&&calls==1&&seen==f.inventory.data(),"correct typed receiver calls native cancel once, including false result");
    f.sender[0x70]=0;Require(f.Cancel(cancelled)==0&&!cancelled&&calls==1,"hidden UI owns its cleanup");f.sender[0x70]=1;
    f.inventory[0x28]=43;Require(f.Cancel(cancelled)==0&&!cancelled&&calls==1,"changed item refused");f.inventory[0x28]=42;
    f.inventory[0x35]=0;Require(f.Cancel(cancelled)==0&&!cancelled&&calls==1,"finished drag not cancelled");f.inventory[0x35]=1;
    f.sender[0]^=1;Require(f.Cancel(cancelled)==ERROR_INVALID_ADDRESS&&!cancelled&&calls==1,"wrong sender type refused");f.sender[0]^=1;
    f.inventory[0]^=1;Require(f.Cancel(cancelled)==ERROR_INVALID_ADDRESS&&!cancelled&&calls==1,"wrong inventory type refused");
    f.capture.receiver=1;Require(f.Cancel(cancelled)!=0&&!cancelled&&calls==1,"unreadable receiver caught");
    BeginInventoryPointerEvent(1);Require(gPointerHeld,"press owns later queued callbacks");EndInventoryPointerEvent(1);Require(gPointerHeld,"returning from dispatch retains ownership");
    EndInventoryPointerEvent(2);Require(!gPointerHeld&&!gCapture.receiver,"release ends ownership");
    std::cout<<"inventory_pointer_capture passed\n";
}
