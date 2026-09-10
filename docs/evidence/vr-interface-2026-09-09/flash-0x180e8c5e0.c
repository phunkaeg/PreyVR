
void FUN_180e8c5e0(longlong param_1,char param_2)

{
  LPCRITICAL_SECTION lpCriticalSection;
  bool bVar1;
  char cVar2;
  uint uVar3;
  longlong *plVar4;
  uint uVar5;
  float fVar6;
  char local_res10 [8];
  
  lpCriticalSection = *(LPCRITICAL_SECTION *)(param_1 + 0xe8);
  EnterCriticalSection(lpCriticalSection);
  plVar4 = (longlong *)FUN_180863f70();
  local_res10[0] = '\0';
  (**(code **)(*plVar4 + 0x888))(plVar4,0xb,local_res10,1,0,0);
  if ((*(longlong *)(param_1 + 0xa8) != 0) && (local_res10[0] == '\0')) {
    uVar5 = *(uint *)(param_1 + 0x3c) & 0xffffffef;
    if (((*(byte *)(param_1 + 0x14) & 1) == 0) || (DAT_182277984 == 0)) {
      bVar1 = false;
    }
    else {
      bVar1 = true;
    }
    uVar3 = uVar5 | 0x10;
    if (!bVar1) {
      uVar3 = uVar5;
    }
    if (uVar3 != *(uint *)(param_1 + 0x3c)) {
      *(uint *)(param_1 + 0x3c) = uVar3;
    }
    if (DAT_182277988 != *(float *)(param_1 + 0x38)) {
      if (DAT_182277988 < 1.0) {
        DAT_182277988 = 1.0;
      }
      fVar6 = DAT_182277988;
      if (1e+06 <= DAT_182277988) {
        fVar6 = 1e+06;
      }
      if (fVar6 <= 1e-06) {
        fVar6 = 1e-06;
      }
      *(float *)(param_1 + 0x38) = fVar6;
    }
    FUN_180dbd980(*(undefined8 *)(param_1 + 0xb8),*(undefined4 *)(param_1 + 0x10));
    FUN_180db8a30(*(undefined8 *)(param_1 + 0xb8),*(byte *)(param_1 + 0x14) >> 1 & 1);
    FUN_180dbc050(*(undefined8 *)(param_1 + 0xb8),*(byte *)(param_1 + 0x14) >> 2 & 1);
    FUN_180dbc280(*(undefined8 *)(param_1 + 0xb8),*(byte *)(param_1 + 0x15) >> 5 & 1);
    cVar2 = FUN_18009fe30(&DAT_182b19370);
    if (cVar2 != '\0') {
      (**(code **)(**(longlong **)(param_1 + 0xa8) + 0x130))();
      LeaveCriticalSection((LPCRITICAL_SECTION)&DAT_182b19370);
    }
  }
  LeaveCriticalSection(lpCriticalSection);
  if (param_2 != '\0') {
    (**(code **)(*(longlong *)(param_1 + -8) + 8))(param_1 + -8);
  }
  return;
}

