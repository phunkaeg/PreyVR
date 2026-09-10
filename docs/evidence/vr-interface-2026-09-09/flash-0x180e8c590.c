
void FUN_180e8c590(undefined8 *param_1)

{
  longlong *plVar1;
  
  if (DAT_182277980 != 0) {
    plVar1 = (longlong *)FUN_180863f70(param_1[0x18]);
    (**(code **)*param_1)(param_1);
    (**(code **)(*plVar1 + 0x300))(plVar1,param_1 + 1);
  }
  return;
}

