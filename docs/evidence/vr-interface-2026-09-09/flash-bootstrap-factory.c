
undefined8 * FUN_180e88e40(void)

{
  undefined8 *puVar1;
  undefined8 *puVar2;
  undefined8 uVar3;
  
  puVar2 = (undefined8 *)FUN_181b77290(0x30);
  if (puVar2 != (undefined8 *)0x0) {
    *puVar2 = &PTR_FUN_181db5a58;
    puVar2[1] = 0;
    puVar2[2] = 0;
    puVar1 = puVar2 + 3;
    puVar2[5] = 0;
    *puVar1 = puVar1;
    puVar2[4] = puVar1;
    uVar3 = FUN_180e8f9d0();
    uVar3 = FUN_180e8f9e0(uVar3,0);
    if (puVar2[2] != 0) {
      FUN_180e8fb70();
    }
    puVar2[2] = uVar3;
    puVar2[5] = puVar2;
    EnterCriticalSection((LPCRITICAL_SECTION)&DAT_182277a38);
    *puVar1 = PTR_LOOP_182277a20;
    puVar2[4] = &PTR_LOOP_182277a20;
    *(undefined8 **)(PTR_LOOP_182277a20 + 8) = puVar1;
    PTR_LOOP_182277a20 = (undefined *)puVar1;
    LeaveCriticalSection((LPCRITICAL_SECTION)&DAT_182277a38);
    return puVar2;
  }
  return (undefined8 *)0x0;
}

