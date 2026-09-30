#pragma once
#include <windows.h>
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
DWORD SetPsiTargetMode(unsigned mode);
unsigned PsiTargetMode();
DWORD SetMedkitSlot(unsigned enabled);
unsigned MedkitSlotEnabled();
bool MedkitOwnsGrip();
void UpdatePsiMedkit(const GameplayPoseFrame&,bool valid);
std::string PsiMedkitReport();
}
