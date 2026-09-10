
void FUN_180e8c770(longlong param_1,int param_2,char param_3,char param_4)

{
  longlong *plVar1;
  undefined8 *puVar2;
  char local_res18 [8];
  
  plVar1 = (longlong *)FUN_180863f70(*(undefined8 *)(param_1 + 0xb8));
  local_res18[0] = '\0';
  puVar2 = (undefined8 *)((longlong)param_2 * 0x38 + *(longlong *)(param_1 + 0x110));
  (**(code **)(*plVar1 + 0x888))(plVar1,0xb,local_res18,1,0,0);
  if (local_res18[0] == '\0') {
    FUN_180dbd980(*(undefined8 *)(param_1 + 0xb8),*(undefined4 *)(param_1 + 0x10));
    FUN_180db8a30(*(undefined8 *)(param_1 + 0xb8),*(byte *)(param_1 + 0x14) >> 1 & 1);
    FUN_180dbc050(*(undefined8 *)(param_1 + 0xb8),*(byte *)(param_1 + 0x14) >> 2 & 1);
    FUN_180dbc280(*(undefined8 *)(param_1 + 0xb8),*(byte *)(param_1 + 0x15) >> 5 & 1);
    thunk_FUN_180db5790(puVar2,*(undefined8 *)(param_1 + 0xb8),param_3);
  }
  if (param_3 != '\0') {
    if (local_res18[0] != '\0') {
      FUN_180dbc010(puVar2);
    }
    *puVar2 = 0;
    puVar2[1] = 0;
    if ((ulonglong)(puVar2[6] - puVar2[4]) < 0x800) {
      FUN_180db51a0(puVar2 + 2,0x800);
    }
  }
  if (param_4 != '\0') {
    (**(code **)(*(longlong *)(param_1 + -8) + 8))(param_1 + -8);
  }
  return;
}

