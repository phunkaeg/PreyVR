
void FUN_18161bf70(longlong param_1)

{
  char *pcVar1;
  int iVar2;
  longlong *plVar3;
  longlong lVar4;
  longlong lVar5;
  longlong lVar6;
  size_t _Size;
  undefined1 local_38 [8];
  undefined1 *local_30;
  undefined2 *local_28;
  undefined1 local_20 [8];
  undefined1 *local_18;
  undefined4 local_10;
  size_t sVar7;
  
  lVar6 = 0;
  if (*(int *)(param_1 + 0x38) == 1) {
    FUN_18162bab0(*(undefined8 *)(param_1 + 0x18));
    *(undefined4 *)(param_1 + 0x38) = 0;
    FUN_18161d4f0(param_1,*(undefined4 *)(*(longlong *)(param_1 + 0x18) + 0x28));
    (*(code *)**(undefined8 **)(param_1 + 8))(param_1 + 8,1);
  }
  plVar3 = (longlong *)
           (**(code **)(*DAT_18224dad0 + 0x60))
                     (DAT_18224dad0,PTR_s_DanielleInventoryExternal_18228c7c0);
  FUN_1802b2870(local_38);
  local_28 = &DAT_18224df7c;
  local_30 = &DAT_18224d03c;
  FUN_1802b27f0(local_20);
  local_18 = &DAT_18224d03c;
  sVar7 = 0xffffffffffffffff;
  do {
    _Size = sVar7 + 1;
    pcVar1 = &DAT_181ca2d79 + sVar7;
    sVar7 = _Size;
  } while (*pcVar1 != '\0');
  if (_Size != 0) {
    FUN_180095610(&local_18,_Size);
    if (local_18 != &DAT_181ca2d78) {
      memcpy(local_18,&DAT_181ca2d78,_Size);
    }
  }
  local_10 = 8;
  (**(code **)(*plVar3 + 0x210))(plVar3,PTR_s_inventoryClose_18228c7d8,local_38,0,0);
  iVar2 = *(int *)(local_18 + -0xc);
  if ((-1 < iVar2) && (iVar2 = iVar2 + -1, *(int *)(local_18 + -0xc) = iVar2, iVar2 < 1)) {
    FUN_1800a8b20();
  }
  FUN_1802b2650(local_20,0,0);
  iVar2 = *(int *)(local_28 + -6);
  if ((-1 < iVar2) && (iVar2 = iVar2 + -1, *(int *)(local_28 + -6) = iVar2, iVar2 < 1)) {
    FUN_1800a8b20();
  }
  iVar2 = *(int *)(local_30 + -0xc);
  if ((-1 < iVar2) && (iVar2 = iVar2 + -1, *(int *)(local_30 + -0xc) = iVar2, iVar2 < 1)) {
    FUN_1800a8b20();
  }
  FUN_1802b24b0(local_38);
  FUN_1802b2720(local_38,0,0);
  *(undefined1 *)(param_1 + 0x34) = 0;
  FUN_18162bb60(*(undefined8 *)(param_1 + 0x18));
  FUN_1811c1f40(*(undefined8 *)(DAT_182c16840 + 0x308),param_1 + 0x10);
  lVar5 = *(longlong *)(DAT_182c16840 + 0x1a8);
  FUN_18182db20(lVar5,param_1);
  if (*(int *)(lVar5 + 0x28) == 2) {
    (**(code **)(*plVar3 + 0xf0))(plVar3,1);
  }
  if (*(longlong *)(DAT_182c16840 + 0x1a8) != 0) {
    lVar4 = FUN_18182c080(*(longlong *)(DAT_182c16840 + 0x1a8),"ArkUIHUD");
    lVar5 = lVar4 + -0x18;
    if (lVar4 != 0) goto LAB_18161c165;
  }
  lVar5 = lVar6;
LAB_18161c165:
  FUN_181669450(lVar5,0);
  if (*(longlong *)(DAT_182c16840 + 0x1a8) != 0) {
    lVar5 = FUN_18182c080(*(longlong *)(DAT_182c16840 + 0x1a8),"ArkSubtitleHandler");
    if (lVar5 != 0) {
      lVar6 = lVar5 + -8;
    }
  }
  lVar6 = FUN_18165f230(lVar6);
  if (lVar6 != 0) {
    FUN_181330370(lVar6,"HUDAlpha",100);
  }
  FUN_1813fe880(*(undefined8 *)(DAT_182c16840 + 0x380),0x8814b57655ca1d1f,1);
  return;
}

