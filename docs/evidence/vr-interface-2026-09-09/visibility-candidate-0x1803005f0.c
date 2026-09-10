
void FUN_1803005f0(longlong *param_1,char param_2)

{
  longlong *plVar1;
  char cVar2;
  undefined8 uVar3;
  longlong *plVar4;
  ulonglong uVar5;
  longlong *plVar6;
  char *pcVar7;
  ulonglong uVar8;
  longlong *plVar9;
  
  if (((DAT_18247fd34 == 0) ||
      (((char)param_1[0xe] == param_2 && (*(char *)((longlong)param_1 + 0x51) == '\0')))) ||
     ((param_2 != '\0' &&
      ((param_1[0xb] == 0 && ((**(code **)(*param_1 + 0x70))(param_1,1), param_1[0xb] == 0)))))) {
    return;
  }
  *(char *)(param_1 + 0xe) = param_2;
  *(undefined1 *)((longlong)param_1 + 0x51) = 0;
  if (DAT_18247fd30 != 0) {
    pcVar7 = "false";
    if (param_2 != '\0') {
      pcVar7 = "true";
    }
    uVar3 = (**(code **)(*param_1 + 0x48))(param_1);
    FUN_1802d6d10(0,"%s (%i): Set visible: %s",uVar3,(int)param_1[0x19],pcVar7);
  }
  plVar1 = (longlong *)param_1[0xb];
  if (plVar1 == (longlong *)0x0) goto LAB_180300758;
  if (((char)param_1[0xe] == '\0') ||
     (cVar2 = (**(code **)(*plVar1 + 0x138))(plVar1,"cry_onShow"), cVar2 == '\0')) {
    cVar2 = (**(code **)(*(longlong *)param_1[0xb] + 0x138))((longlong *)param_1[0xb],"cry_onHide");
    if (cVar2 != '\0') {
      if (DAT_18247fd30 != 0) {
        uVar3 = (**(code **)(*param_1 + 0x48))(param_1);
        FUN_1802d6d10(0,"%s (%i): UIElement invoke \"cry_onHide\"",uVar3);
      }
      pcVar7 = "cry_onHide";
      goto LAB_18030072f;
    }
  }
  else {
    if (DAT_18247fd30 != 0) {
      uVar3 = (**(code **)(*param_1 + 0x48))(param_1);
      FUN_1802d6d10(0,"%s (%i): UIElement invoke \"cry_onShow\"",uVar3);
    }
    pcVar7 = "cry_onShow";
LAB_18030072f:
    (**(code **)(*(longlong *)param_1[0xb] + 0x158))((longlong *)param_1[0xb],pcVar7,0,0,0);
  }
  (**(code **)(*(longlong *)param_1[0xb] + 0x70))((longlong *)param_1[0xb],0);
LAB_180300758:
  cVar2 = (**(code **)(*param_1 + 0xf8))(param_1,0x21000);
  if (cVar2 != '\0') {
    FUN_1802d8e00(param_1[5],param_2);
  }
  plVar1 = param_1 + 0x33;
  uVar8 = 0;
  param_1[0x36] = param_1[0x36] + 1;
  plVar6 = (longlong *)*plVar1;
  uVar5 = param_1[0x34] - (longlong)plVar6 >> 3;
  if ((uVar5 != 0) && (plVar9 = (longlong *)*plVar6, plVar9 != (longlong *)0x0)) goto LAB_1803007e3;
  plVar9 = (longlong *)0x0;
  uVar8 = 1;
  plVar4 = plVar6;
  if (1 < uVar5) {
    do {
      plVar4 = plVar4 + 1;
      if (*plVar4 != 0) goto LAB_1803007c6;
      uVar8 = uVar8 + 1;
    } while (uVar8 < uVar5);
  }
LAB_1803007d0:
  do {
    do {
      if ((plVar9 == (longlong *)0x0) &&
         ((uVar5 <= uVar8 || (plVar9 = (longlong *)plVar6[uVar8], plVar9 == (longlong *)0x0)))) {
        FUN_1802d5b80(plVar1);
        FUN_1802d5e20(param_1[5]);
        FUN_1803012c0(param_1);
        if ((char)param_1[0xe] == '\0') {
          return;
        }
        (**(code **)(*param_1 + 0x1e0))(param_1);
        return;
      }
LAB_1803007e3:
      (**(code **)(*plVar9 + 0x20))(plVar9,param_1,(char)param_1[0xe]);
      plVar6 = (longlong *)*plVar1;
      plVar9 = (longlong *)0x0;
      uVar5 = param_1[0x34] - (longlong)plVar6 >> 3;
      uVar8 = uVar8 + 1;
    } while (uVar5 <= uVar8);
    plVar4 = plVar6 + uVar8;
    do {
      if (*plVar4 != 0) goto LAB_1803007c6;
      uVar8 = uVar8 + 1;
      plVar4 = plVar4 + 1;
    } while (uVar8 < uVar5);
  } while( true );
LAB_1803007c6:
  plVar9 = (longlong *)*plVar4;
  goto LAB_1803007d0;
}

