#include "preyvr/RenderContract.h"
#include "preyvr/SettingReadback.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
void Require(bool v,const char* msg) {if(!v) {std::cerr<<msg<<"\n";std::exit(1);}}
int main() {
    RenderContract a{};a.eye=0;a.valid=true;a.displayTime=100;a.tangents={-1,1.2f,1.1f,-.9f};
    RenderContract b=a;b.eye=1;b.displayTime=200;b.pose.position.x=3;
    RenderContractQueue queue;RenderContract out;
    Require(ValidRenderContract(a),"valid asymmetric camera rejected");
    Require(queue.Push(a)&&queue.Push(b),"publish");
    a.pose.position.x=999; // producer moves again before the consumer runs
    Require(queue.Pop(out)&&out.pose.position.x==0&&out.displayTime==100&&out.eye==0,"retimed older pixels");
    Require(queue.Pop(out)&&out.pose.position.x==3&&out.displayTime==200&&out.eye==1,"second frame mismatch");
    Require(!queue.Pop(out)&&!out.valid,"underflow reused metadata");
    b.tangents[0]=std::numeric_limits<float>::quiet_NaN();Require(!ValidRenderContract(b),"NaN FOV accepted");
    b=a;b.tangents[0]=.2f;Require(!ValidRenderContract(b),"inverted FOV accepted");
    b=a;b.valid=false;Require(queue.Push(b)&&queue.Push(a),"refused frame placeholders");
    Require(queue.Pop(out)&&!ValidRenderContract(out),"refusal lost");
    Require(queue.Pop(out)&&out.displayTime==100,"refusal shifted next frame");
    for(int i=0;i<64;++i)Require(queue.Push(a),"capacity");
    Require(!queue.Push(a)&&!queue.Pop(out),"overflow silently skipped records");
    queue.Reset();Require(queue.Push(a)&&queue.Pop(out),"reset failed");
    b=a;b.pose.orientation.w=0;Require(!ValidRenderContract(b),"invalid pose accepted");
    b=a;b.displayTime=0;Require(!ValidRenderContract(b),"missing sample time accepted");
    SettingReadback readback{1,1000};
    Require(readback.Observe(false,1,1)==ReadbackResult::pending,"unread value accepted");
    Require(readback.Observe(true,3,999)==ReadbackResult::pending,"queue acknowledgement accepted");
    Require(readback.Observe(true,1,500)==ReadbackResult::verified,"applied value rejected");
    Require(readback.Observe(true,3,1000)==ReadbackResult::failed,"ignored setting did not fail");
    Require(readback.Observe(false,1,1000)==ReadbackResult::failed,"missing CVar did not fail");
    std::cout<<"Render contract and CVar readback regressions passed\n";
}
