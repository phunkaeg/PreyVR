
void FUN_181378c80(longlong param_1)

{
  char *pcVar1;
  undefined8 uVar2;
  int iVar3;
  longlong *plVar4;
  longlong lVar5;
  size_t _Size;
  int local_res10 [2];
  undefined1 local_48 [8];
  undefined1 *local_40;
  undefined2 *local_38;
  undefined1 local_30 [8];
  undefined1 *local_28;
  undefined4 local_20;
  size_t sVar6;
  
  plVar4 = (longlong *)(**(code **)(*DAT_18224dad0 + 0x60))(DAT_18224dad0,"DaniellePDA");
  if (plVar4 != (longlong *)0x0) {
    lVar5 = GetArkPlayerInstance();
    FUN_1810839f0(lVar5 + 0x678);
    lVar5 = FUN_1815ad020();
    uVar2 = *(undefined8 *)(param_1 + 0x28);
    local_res10[0] = (*(char *)(lVar5 + 0x10a) != '\0') + 1;
    FUN_1802b2870(local_48);
    local_38 = &DAT_18224df7c;
    local_40 = &DAT_18224d03c;
    FUN_1802b27f0(local_30);
    local_28 = &DAT_18224d03c;
    sVar6 = 0xffffffffffffffff;
    do {
      _Size = sVar6 + 1;
      pcVar1 = &DAT_181ca2d79 + sVar6;
      sVar6 = _Size;
    } while (*pcVar1 != '\0');
    if (_Size != 0) {
      FUN_180095610(&local_28,_Size);
      if (local_28 != &DAT_181ca2d78) {
        memcpy(local_28,&DAT_181ca2d78,_Size);
      }
    }
    local_20 = 8;
    FUN_1802de840(local_48,local_res10);
    (**(code **)(*plVar4 + 0x210))(plVar4,uVar2,local_48,0,0);
    iVar3 = *(int *)(local_28 + -0xc);
    if ((-1 < iVar3) && (iVar3 = iVar3 + -1, *(int *)(local_28 + -0xc) = iVar3, iVar3 < 1)) {
      FUN_1800a8b20();
    }
    FUN_1802b2650(local_30,0,0);
    iVar3 = *(int *)(local_38 + -6);
    if ((-1 < iVar3) && (iVar3 = iVar3 + -1, *(int *)(local_38 + -6) = iVar3, iVar3 < 1)) {
      FUN_1800a8b20();
    }
    iVar3 = *(int *)(local_40 + -0xc);
    if ((-1 < iVar3) && (iVar3 = iVar3 + -1, *(int *)(local_40 + -0xc) = iVar3, iVar3 < 1)) {
      FUN_1800a8b20();
    }
    FUN_1802b24b0(local_48);
    FUN_1802b2720(local_48,0,0);
  }
  FUN_18139f110(param_1);
  return;
}

