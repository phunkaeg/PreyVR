bool FUN_1802fc980(longlong *param_1,char param_2)

{
  longlong *plVar1;
  longlong lVar2;
  char cVar3;
  undefined1 uVar4;
  undefined4 uVar5;
  undefined4 uVar6;
  undefined4 uVar7;
  undefined8 uVar8;
  longlong *plVar9;
  undefined8 *puVar10;
  longlong lVar11;
  longlong lVar12;
  longlong *plVar13;
  longlong *plVar14;
  longlong *plVar15;
  longlong *plVar16;
  longlong *plVar17;
  longlong *local_res8 [2];
  undefined8 local_res18;
  undefined8 local_res20;
  undefined8 in_stack_ffffffffffffff98;
  undefined8 *local_58;
  longlong local_50;
  undefined4 local_48;
  undefined4 uStack_44;
  undefined4 local_40;
  
  uVar7 = (undefined4)((ulonglong)in_stack_ffffffffffffff98 >> 0x20);
  cVar3 = (**(code **)(*param_1 + 0x98))();
  if (cVar3 == '\0') {
    uVar8 = (**(code **)(*param_1 + 0x48))();
    FUN_1802d6d10(1,"%s: Element is marked as not valid! Instance was not created!",uVar8);
    return false;
  }
  plVar9 = (longlong *)FUN_1802fcf30(param_1);
  if ((param_2 == '\0') || (plVar9 == (longlong *)0x0)) {
    return plVar9 != (longlong *)0x0;
  }
  uVar4 = (**(code **)(*plVar9 + 0x28))(plVar9,"CE_NoAutoUpdate");
  FUN_1802fffb0(param_1,0x2000,uVar4);
  if (DAT_18247fd34 == 0) {
    return false;
  }
  if (param_1[0xb] != 0) {
    return true;
  }
  plVar9 = (longlong *)(**(code **)(*plVar9 + 0x10))(plVar9,0x1c);
  param_1[0xb] = (longlong)plVar9;
  lVar12 = CONCAT44(uVar7,1);
  (**(code **)(*plVar9 + 0x30))(plVar9,0xffffffff,0xffffffff,1,lVar12,0x3f800000);
  FUN_1803012c0(param_1);
  if (param_1[0xb] == 0) {
    lVar12 = param_1[8];
    uVar8 = (**(code **)(*param_1 + 0x48))(param_1);
    FUN_1802d6d10(2,"%s (%i): Failed to created flash instance: \"%s\"",uVar8,(int)param_1[0x19],
                  lVar12);
    return false;
  }
  if (DAT_18247fd30 != 0) {
    lVar12 = param_1[8];
    uVar8 = (**(code **)(*param_1 + 0x48))(param_1);
    FUN_1802d6d10(0,"%s (%i): Created flash instance: \"%s\"",uVar8,(int)param_1[0x19],lVar12);
  }
  (**(code **)(*(longlong *)param_1[0xb] + 0x20))((longlong *)param_1[0xb],(int)param_1[9]);
  (**(code **)(*(longlong *)param_1[0xb] + 0xe0))((longlong *)param_1[0xb],param_1 + 1,0);
  plVar9 = (longlong *)0x0;
  local_res18 = 0;
  (**(code **)(*(longlong *)param_1[0xb] + 0x128))
            ((longlong *)param_1[0xb],*(undefined8 *)param_1[0xd],&local_res18);
  uVar8 = FUN_1802f7920(local_res8,*(undefined8 *)param_1[0xd]);
  puVar10 = (undefined8 *)FUN_1802f8750(param_1 + 0x38,uVar8);
  *puVar10 = local_res18;
  if ((local_res8[0] != (longlong *)0x0) &&
     (*(int *)((longlong)local_res8[0] + -0xc) = *(int *)((longlong)local_res8[0] + -0xc) + -1,
     *(int *)((longlong)local_res8[0] + -0xc) < 1)) {
    (**(code **)(*DAT_18224da90 + 0x18))();
  }
  uVar8 = FUN_1802f7920(local_res8,*(undefined8 *)param_1[0xd]);
  lVar11 = FUN_1802f8750(param_1 + 0x38,uVar8);
  *(undefined8 *)(lVar11 + 8) = 0;
  if ((local_res8[0] != (longlong *)0x0) &&
     (*(int *)((longlong)local_res8[0] + -0xc) = *(int *)((longlong)local_res8[0] + -0xc) + -1,
     *(int *)((longlong)local_res8[0] + -0xc) < 1)) {
    (**(code **)(*DAT_18224da90 + 0x18))();
  }
  plVar1 = (longlong *)param_1[0x3a];
  local_50 = param_1[0xd];
  local_res20 = 0;
  cVar3 = *(char *)(plVar1[1] + 0x19);
  plVar15 = plVar1;
  plVar14 = (longlong *)plVar1[1];
  while (cVar3 == '\0') {
    cVar3 = *(char *)(*plVar14 + 0x19);
    plVar15 = plVar14;
    plVar14 = (longlong *)*plVar14;
  }
  if ((plVar15 == plVar1) || (plVar15[4] != 0)) {
    local_58 = &local_res20;
    lVar12 = FUN_1802f4ce0(param_1 + 0x3a,&DAT_181caaa40,&local_58,local_res8);
    FUN_1802f5840(param_1 + 0x3a,local_res8,plVar15,lVar12 + 0x20,lVar12);
    plVar15 = local_res8[0];
  }
  uVar8 = *(undefined8 *)param_1[0xd];
  puVar10 = (undefined8 *)FUN_1802f8610(plVar15 + 5,&local_50);
  uVar8 = FUN_1802f7920(&local_48,uVar8);
  uVar8 = FUN_1802f8750(param_1 + 0x38,uVar8);
  *puVar10 = uVar8;
  lVar11 = CONCAT44(uStack_44,local_48);
  if ((lVar11 != 0) &&
     (*(int *)(lVar11 + -0xc) = *(int *)(lVar11 + -0xc) + -1, *(int *)(lVar11 + -0xc) < 1)) {
    (**(code **)(*DAT_18224da90 + 0x18))();
  }
  EnterCriticalSection((LPCRITICAL_SECTION)(param_1 + 0x44));
  lVar11 = param_1[0x41];
  lVar2 = param_1[0x42];
  LeaveCriticalSection((LPCRITICAL_SECTION)(param_1 + 0x44));
  if (lVar11 == lVar2) {
    (**(code **)(*param_1 + 0x1e0))(param_1);
  }
  if (DAT_18247fd30 != 0) {
    uVar8 = (**(code **)(*param_1 + 0x48))(param_1);
    FUN_1802d6d10(0,"%s (%i): UIElement invoke \"cry_onSetup\"",uVar8,(int)param_1[0x19]);
  }
  cVar3 = (**(code **)(*(longlong *)param_1[0xb] + 0x138))((longlong *)param_1[0xb],"cry_onSetup");
  if (cVar3 != '\0') {
    local_48 = 3;
    local_40 = 0;
    lVar12 = 0;
    (**(code **)(*(longlong *)param_1[0xb] + 0x158))
              ((longlong *)param_1[0xb],"cry_onSetup",&local_48,1,0);
  }
  plVar1 = param_1 + 0x33;
  param_1[0x36] = param_1[0x36] + 1;
  plVar15 = (longlong *)*plVar1;
  plVar14 = (longlong *)(param_1[0x34] - (longlong)plVar15 >> 3);
  if ((plVar14 != (longlong *)0x0) &&
     (plVar17 = (longlong *)*plVar15, plVar16 = plVar9, plVar17 != (longlong *)0x0))
  goto LAB_1802fcde3;
  plVar16 = (longlong *)0x1;
  plVar17 = plVar9;
  plVar13 = plVar15;
  if ((longlong *)0x1 < plVar14) {
    do {
      plVar13 = plVar13 + 1;
      if (*plVar13 != 0) goto LAB_1802fcdca;
      plVar16 = (longlong *)((longlong)plVar16 + 1);
    } while (plVar16 < plVar14);
  }
LAB_1802fcdd0:
  do {
    do {
      uVar7 = (undefined4)((ulonglong)lVar12 >> 0x20);
      if ((plVar17 == (longlong *)0x0) &&
         ((plVar14 <= plVar16 ||
          (plVar17 = (longlong *)plVar15[(longlong)plVar16], plVar17 == (longlong *)0x0)))) {
        FUN_1802d5b80(plVar1);
        EnterCriticalSection((LPCRITICAL_SECTION)(param_1 + 0x44));
        lVar12 = param_1[0x41];
        lVar11 = param_1[0x42];
        LeaveCriticalSection((LPCRITICAL_SECTION)(param_1 + 0x44));
        if (lVar12 != lVar11) {
          (**(code **)(*(longlong *)param_1[0xb] + 0x40))((longlong *)param_1[0xb],2);
          plVar9 = (longlong *)param_1[0xb];
          lVar12 = *plVar9;
          uVar5 = (**(code **)(lVar12 + 0x1a0))(plVar9);
          uVar6 = (**(code **)(*plVar9 + 0x198))(plVar9);
          uVar8 = CONCAT44(uVar7,uVar5);
          (**(code **)(lVar12 + 0x30))(param_1[0xb],0,0,uVar6,uVar8,0x3f800000);
          uVar6 = (undefined4)((ulonglong)uVar8 >> 0x20);
          plVar9 = (longlong *)param_1[0xb];
          lVar12 = *plVar9;
          uVar7 = (**(code **)(lVar12 + 0x1a0))(plVar9);
          uVar5 = (**(code **)(*plVar9 + 0x198))(plVar9);
          (**(code **)(lVar12 + 0x60))(param_1[0xb],0,0,uVar5,CONCAT44(uVar6,uVar7));
          (**(code **)(*param_1 + 0xe0))(param_1,1);
        }
        return true;
      }
LAB_1802fcde3:
      (**(code **)(*plVar17 + 0x10))(plVar17,param_1);
      plVar15 = (longlong *)*plVar1;
      plVar14 = (longlong *)(param_1[0x34] - (longlong)plVar15 >> 3);
      plVar16 = (longlong *)((longlong)plVar16 + 1);
      plVar17 = plVar9;
    } while (plVar14 <= plVar16);
    plVar13 = plVar15 + (longlong)plVar16;
    do {
      if (*plVar13 != 0) goto LAB_1802fcdca;
      plVar16 = (longlong *)((longlong)plVar16 + 1);
      plVar13 = plVar13 + 1;
      plVar17 = (longlong *)0x0;
    } while (plVar16 < plVar14);
  } while( true );
LAB_1802fcdca:
  plVar17 = (longlong *)*plVar13;
  goto LAB_1802fcdd0;
}
