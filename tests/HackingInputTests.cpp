#include "preyvr/HackingInput.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <type_traits>

using namespace preyvr::hacking;
namespace {
void Check(bool value,const char* why) { if (!value) { std::cerr << why << '\n'; std::exit(1); } }
bool Near(float a,float b,float tolerance=.0001f) { return std::fabs(a-b) < tolerance; }
bool Empty(const Output& o) { return o.axisMask==0 && o.pressMask==0 && o.releaseMask==0; }
struct Driver {
    InputState state;
    Input i{true,true,0,0,false,false,false,false,1000000000ull,1000000000ull,1,1,2,1};
    Output Next(bool commit = true)
    {
        i.nowNs += 10000000ull; i.stampNs = i.nowNs; ++i.sequence;
        auto out = state.Update(i); if (commit) { state.Commit(out); } return out;
    }
    void Ready()
    {
        const auto first = Next(); Check(first.axisMask==(AxisX|AxisY)&&!first.ready, "entry primes neutral axes first");
        Check(Next().ready, "neutral source after acknowledged zeros arms");
    }
};
void Basic()
{
    Driver d; d.Ready();
    d.i.stickX = .575f;
    auto o = d.Next();
    Check(o.ready&&o.axisMask==AxisX&&Near(o.x,.5f)&&Near(o.y,0), "actual analog travel is preserved after deadzone");
    d.i.stickX = .83f; o = d.Next(); Check(Near(o.x,.8f)&&o.axisMask==AxisX, "two deflections remain distinguishable");
    d.i.stickX = d.i.stickY = 1; o = d.Next();
    Check(Near(std::hypot(o.x,o.y),1)&&Near(o.x,o.y), "square-gated diagonal is radially bounded");
    d.i.stickX = .01f; d.i.stickY = -.01f; o = d.Next();
    Check(o.axisMask==(AxisX|AxisY)&&o.x==0&&o.y==0, "return to deadzone emits hard zero");
    d.i.a = true; d.i.y = true; o = d.Next();
    Check(o.pressMask==(ButtonA|ButtonY)&&o.releaseMask==0, "face buttons press as native held state");
    Check(Empty(d.state.Update(d.i)), "accepted duplicate source emits no repeated press");
    Check(Empty(d.Next()), "held button remains down without repeated edge");
    d.i.a = false; o = d.Next(); Check(o.releaseMask==ButtonA&&!o.pressMask, "button release independent of other held button");
    d.i.y = false; o = d.Next(); Check(o.releaseMask==ButtonY, "remaining held button releases");
    d.i.stickX = 100; d.i.stickY = -100; o = d.Next();
    Check(std::isfinite(o.x)&&std::isfinite(o.y)&&std::hypot(o.x,o.y)<=1.00001f, "finite overrange readings remain bounded");
    d.i.stickX = NAN; o = d.Next(); Check(o.x==0&&o.y==0&&!o.ready, "nonfinite reading releases rather than freezing native axes");
}
void EntryAndTransitions()
{
    Driver d; d.i.active = false; d.i.a = true; d.i.stickX = 1;
    Check(Empty(d.Next()), "ordinary pause/menu is not hacking input");
    d.i.active = true;
    auto o = d.Next(); Check(!o.ready&&!o.pressMask&&o.x==0, "held controls on hacking entry are quarantined");
    Check(!d.Next().ready, "held entry remains blocked");
    d.i.a = false; d.i.stickX = 0; Check(d.Next().ready, "neutral releases entry quarantine");
    d.i.b = true; d.i.stickY = -.8f; d.Next();
    d.i.active = false; o = d.Next(false);
    Check(o.releaseMask==ButtonB&&(o.axisMask&AxisY)&&o.y==0, "leaving hacking releases owned channels");
    d.i.active = true; ++d.i.generation; d.i.b = false; d.i.stickY = 0;
    auto blocked = d.Next(false); Check(!blocked.ready&&blocked.releaseMask==ButtonB, "new movie cannot arm before old release accepted");
    d.state.Commit(blocked); Check(d.Next().ready, "new neutral movie acquires after cleanup");
    for (int context = 0; context < 3; ++context) {
        Driver one; one.Ready(); one.i.x = true; one.i.stickX = .7f; one.Next();
        if(context==0)++one.i.epoch; if(context==1)one.i.reference+=2; if(context==2)++one.i.generation;
        o = one.Next(); Check(!o.ready&&o.releaseMask==ButtonX&&o.x==0, "context change releases old ownership");
        Check(!one.Next().ready, "new context held controls remain blocked");
        one.i.x = false; one.i.stickX = 0; Check(one.Next().ready, "fresh neutral rearms changed context");
    }
}
void Delivery()
{
    Driver d; d.Ready(); d.i.a = d.i.b = true; d.i.stickX = .6f; d.i.stickY = .3f;
    auto o = d.Next(false);
    auto partial = o; partial.pressMask = ButtonA; partial.axisMask = AxisX;
    d.state.Commit(partial);
    Check(d.state.OwnsControls(), "delivered nonzero axis/button owns native controls");
    auto retry = d.state.Update(d.i);
    Check(retry.pressMask==ButtonB&&retry.axisMask==AxisY, "only refused press/axis retries on duplicate source");
    d.state.Commit(retry); Check(Empty(d.state.Update(d.i)), "accepted channels do not repeat");

    d.i.active = false; auto release = d.Next(false);
    partial = release; partial.releaseMask = ButtonA; partial.axisMask = AxisY;
    d.state.Commit(partial);
    retry = d.state.Update(d.i);
    Check(retry.releaseMask==ButtonB&&retry.axisMask==AxisX&&retry.x==0, "partial cleanup retains refused release and zero");
    Check(d.state.OwnsControls(), "partial failed cleanup keeps native interlock owned");
    d.state.Commit(release); // out of order: a newer proposal already exists
    auto afterOld = d.state.Update(d.i);
    Check(afterOld.releaseMask==ButtonB&&afterOld.axisMask==AxisX, "old acknowledgement cannot erase newer cleanup");
    d.state.Commit(afterOld); Check(Empty(d.state.Update(d.i)), "all accepted cleanup clears ownership");
    Check(!d.state.OwnsControls(), "fully acknowledged cleanup frees native interlock");

    Driver refused; refused.Ready(); refused.i.a = true;
    auto proposal = refused.Next(false); Check(proposal.pressMask==ButtonA, "press proposal positive control");
    auto none = proposal; none.pressMask = 0; refused.state.Commit(none);
    Check(refused.state.Update(refused.i).pressMask==ButtonA, "unaccepted press stays retryable");
    refused.i.active = false;
    Check(refused.Next().releaseMask==0, "never-delivered press does not invent owned release");

    InputState prime; Driver source;
    auto init = prime.Update(source.i);
    Check(!prime.OwnsControls(), "unacknowledged initial zeros do not own native controls");
    partial = init; partial.axisMask = AxisX; prime.Commit(partial);
    auto priming = prime.Update(source.i);
    Check(!priming.ready&&priming.axisMask==AxisY, "unaccepted initial zero blocks acquisition");
    prime.Commit(priming); Check(prime.Update(source.i).ready, "partial initial cleanup completes before acquisition");
}
void Expiry()
{
    Driver d; d.Ready(); d.i.a = true; d.i.stickX = .8f; d.Next();
    d.i.nowNs += 201000000ull;
    auto expired = d.state.Update(d.i);
    Check(!expired.ready&&expired.releaseMask==ButtonA&&(expired.axisMask&AxisX), "same-sequence publication expires and releases");
    auto again = d.state.Update(d.i);
    Check(again.releaseMask==ButtonA&&again.axisMask==expired.axisMask, "expired release retries until acknowledged");
    d.state.Commit(again); d.i.a = false; d.i.stickX = 0; d.Next(); Check(d.Next().ready, "fresh neutral recovers after expiry cleanup");

    Driver gap; gap.Ready(); gap.i.x = true; gap.Next(); gap.i.nowNs += 300000000ull;
    auto broken = gap.Next(); Check(!broken.ready&&broken.releaseMask==ButtonX, "fresh sample after long gap still resets owned input");
    Driver regression; regression.Ready(); regression.i.b = true; regression.Next();
    --regression.i.sequence;
    Check(regression.state.Update(regression.i).releaseMask==ButtonB, "out-of-order source cannot hold accepted button");
    Driver loss; loss.Ready(); loss.i.y = true; loss.Next(); loss.i.valid = false;
    Check(loss.Next().releaseMask==ButtonY, "tracking loss releases accepted button");
}
}
int main()
{
    static_assert(std::is_trivially_copyable_v<InputState>);
    Basic(); EntryAndTransitions(); Delivery(); Expiry();
    std::cout << "hacking_input: analog, modal/context ownership, delivery acknowledgement and expiry controls passed\n";
}
