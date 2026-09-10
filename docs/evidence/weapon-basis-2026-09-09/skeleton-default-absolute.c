
undefined * FUN_1808bc0e0(longlong param_1,uint param_2)

{
  longlong *plVar1;
  uint uVar2;
  undefined *puVar3;
  
  plVar1 = (longlong *)(param_1 + 0x18);
  uVar2 = (**(code **)(*(longlong *)(param_1 + 0x18) + 8))(plVar1);
  if (param_2 < uVar2) {
                    /* WARNING: Could not recover jumptable at 0x0001808bc110. Too many branches */
                    /* WARNING: Treating indirect jump as call */
    puVar3 = (undefined *)(**(code **)(*plVar1 + 0x48))(plVar1,param_2);
    return puVar3;
  }
  return &DAT_182257688;
}

