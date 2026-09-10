
void FUN_1814b7fc0(undefined8 *param_1)

{
  longlong *plVar1;
  
  *param_1 = &PTR_FUN_181e44b78;
  param_1[0x1e] = &PTR_StartProfileThreadUsage_181e44c00;
  plVar1 = (longlong *)(**(code **)(*DAT_18224dad0 + 0x60))(DAT_18224dad0,"DaniellePDA");
  if (plVar1 != (longlong *)0x0) {
    (**(code **)(*plVar1 + 0x200))(plVar1,param_1 + 0x1e);
  }
  FUN_1814b1ab0(param_1 + 0x1f);
  param_1[0x1e] = &PTR_StartProfileThreadUsage_181cad168;
  FUN_1814c5dc0(param_1);
  return;
}

