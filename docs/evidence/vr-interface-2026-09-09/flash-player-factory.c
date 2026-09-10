
longlong * FUN_180e89350(longlong param_1,undefined4 param_2,undefined4 param_3)

{
  longlong *plVar1;
  LPCRITICAL_SECTION lpCriticalSection;
  char cVar2;
  longlong lVar3;
  longlong *plVar4;
  undefined8 uVar5;
  
  plVar1 = *(longlong **)(param_1 + 8);
  if ((plVar1 != (longlong *)0x0) && (lVar3 = FUN_181b77290(400), lVar3 != 0)) {
    plVar4 = (longlong *)FUN_180e873a0(lVar3);
    if (plVar4 == (longlong *)0x0) {
      return (longlong *)0x0;
    }
    lpCriticalSection = (LPCRITICAL_SECTION)plVar4[0x1e];
    EnterCriticalSection(lpCriticalSection);
    uVar5 = (**(code **)(*plVar1 + 0x60))(plVar1);
    cVar2 = FUN_180e88540(plVar4,uVar5,plVar1,param_2,param_3);
    LeaveCriticalSection(lpCriticalSection);
    if (cVar2 != '\0') {
      return plVar4;
    }
    (**(code **)(*plVar4 + 8))(plVar4);
  }
  return (longlong *)0x0;
}

