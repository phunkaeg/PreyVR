#pragma once
#include "preyvr/BodyEquipment.h"
namespace preyvr::equipment {
// Same torso estimate as holsters, with one logical left-hip slot.
class MedkitSlot {
public:
 bool Update(Pose head,Vec3 hand,bool grip,bool trigger,bool valid,float dt){
    if(!valid){Reset();return false;}
    const int zone=zone_.Update(head,hand,grip,valid,dt);
    if(zone>=0){selected_=true;triggerArmed_=!trigger;used_=false;}
    if(!grip||!zone_.OwnsGrip()){selected_=triggerArmed_=used_=false;return false;}
    if(!trigger&&!used_)triggerArmed_=true;
    if(selected_&&triggerArmed_&&!used_&&trigger){used_=true;triggerArmed_=false;return true;}
    return false;
 }
 bool OwnsGrip()const{return selected_&&zone_.OwnsGrip();}
 // Diagnostics (dbg.draw).
 const HolsterGesture& Zone()const{return zone_;}
 bool TriggerArmed()const{return triggerArmed_;}
 void Reset(){zone_.Reset();selected_=triggerArmed_=used_=false;}
private:
 HolsterGesture zone_{{{{-.25f,-.65f,.02f},{-.25f,-.65f,.02f}}}};
 bool selected_=false,triggerArmed_=false,used_=false;
};
}
