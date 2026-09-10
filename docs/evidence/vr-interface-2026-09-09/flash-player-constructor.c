
undefined8 * FUN_180e873a0(undefined8 *param_1)

{
  undefined8 *puVar1;
  undefined8 *puVar2;
  undefined8 uVar3;
  undefined8 *puVar4;
  
  *(byte *)((longlong)param_1 + 0x1c) = *(byte *)((longlong)param_1 + 0x1c) & 0xf8;
  *(byte *)((longlong)param_1 + 0x1c) = *(byte *)((longlong)param_1 + 0x1c) | 8;
  puVar4 = (undefined8 *)0x0;
  *(byte *)((longlong)param_1 + 0x1d) = *(byte *)((longlong)param_1 + 0x1d) & 0xc0;
  *param_1 = &PTR_FUN_181db56d8;
  param_1[1] = &PTR_FUN_181db58b8;
  *(undefined4 *)(param_1 + 2) = 1;
  *(undefined4 *)((longlong)param_1 + 0x14) = 0;
  *(undefined4 *)(param_1 + 3) = 0;
  FUN_18183ba70(param_1 + 4);
  *(undefined4 *)(param_1 + 0xd) = 6;
  param_1[0xb] = &PTR_FUN_181d96678;
  *(undefined4 *)(param_1 + 0xc) = 1;
  param_1[0xb] = &PTR_FUN_181db56b8;
  *(undefined4 *)(param_1 + 0xe) = 8;
  param_1[0xf] = 0;
  param_1[0x10] = 0;
  param_1[0x11] = 0;
  param_1[0x12] = 0;
  param_1[0x13] = 0;
  param_1[0x14] = 0;
  param_1[0x15] = 0;
  param_1[0x16] = 0;
  param_1[0x17] = 0;
  param_1[0x18] = 0;
  puVar1 = (undefined8 *)FUN_181b77290(8);
  puVar2 = puVar4;
  if (puVar1 != (undefined8 *)0x0) {
    *puVar1 = &DAT_18224d03c;
    puVar2 = puVar1;
  }
  param_1[0x19] = 0;
  param_1[0x1a] = 0;
  FUN_180e87330(param_1 + 0x19,puVar2);
  puVar2 = param_1 + 0x1b;
  param_1[0x1d] = 0;
  *puVar2 = puVar2;
  param_1[0x1c] = puVar2;
  puVar1 = (undefined8 *)FUN_181b77290(0x28);
  if (puVar1 != (undefined8 *)0x0) {
    FUN_180098690(puVar1);
    puVar4 = puVar1;
  }
  param_1[0x1e] = 0;
  param_1[0x1f] = 0;
  FUN_180e872c0(param_1 + 0x1e,puVar4);
  param_1[0x20] = 0;
  *(undefined4 *)(param_1 + 0x21) = 0;
  param_1[0x23] = 0;
  uVar3 = FUN_180e8f9d0();
  uVar3 = FUN_180e8f9e0(uVar3,0);
  if (param_1[0x17] != 0) {
    FUN_180e8fb70();
  }
  param_1[0x17] = uVar3;
  uVar3 = FUN_180e8f9d0();
  uVar3 = FUN_180e8fa80(uVar3,0);
  if (param_1[0x18] != 0) {
    FUN_181832ea0();
  }
  param_1[0x18] = uVar3;
  param_1[0x1d] = param_1;
  EnterCriticalSection((LPCRITICAL_SECTION)&DAT_1822779b8);
  *puVar2 = PTR_LOOP_1822779a0;
  param_1[0x1c] = &PTR_LOOP_1822779a0;
  *(undefined8 **)(PTR_LOOP_1822779a0 + 8) = puVar2;
  PTR_LOOP_1822779a0 = (undefined *)puVar2;
  LeaveCriticalSection((LPCRITICAL_SECTION)&DAT_1822779b8);
  return param_1;
}

