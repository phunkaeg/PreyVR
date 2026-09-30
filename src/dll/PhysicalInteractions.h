#pragma once
#include "preyvr/AnimIk.h"
#include "preyvr/WeaponRigAlignment.h"
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void PublishPhysicalWeapon(const GameplayPoseFrame&,const RigIdentity&,const animik::Location&,
                           Pose wristModel,const weaponrig::ContactGeometry&);
void UpdatePhysicalInteractions(const GameplayPoseFrame&,bool allowed);
bool PhysicalInteractionsEnabled();
void SetPhysicalMelee(unsigned);unsigned PhysicalMeleeEnabled();
void SetPhysicalContacts(unsigned);unsigned PhysicalContactsEnabled();
void SetSwingSpeed(unsigned);unsigned SwingSpeed(); // centimetres per second
void SetContactStrength(unsigned);unsigned ContactStrength();
std::string PhysicalInteractionsReport();
}
