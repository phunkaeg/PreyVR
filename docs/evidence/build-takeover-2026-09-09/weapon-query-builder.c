
void * FUN_18124c210(void *param_1,undefined8 *param_2,undefined8 *param_3,undefined4 param_4,
                    undefined8 param_5,uint param_6)

{
  undefined8 uVar1;
  undefined4 uVar2;
  
  memset(param_1,0,0x70);
  uVar2 = *(undefined4 *)(param_2 + 1);
  *(undefined8 *)((longlong)param_1 + 0x18) = *param_2;
  uVar1 = *param_3;
  *(undefined4 *)((longlong)param_1 + 0x20) = uVar2;
  uVar2 = *(undefined4 *)(param_3 + 1);
  *(undefined8 *)((longlong)param_1 + 0x24) = uVar1;
  *(undefined4 *)((longlong)param_1 + 0x2c) = uVar2;
  *(undefined **)((longlong)param_1 + 0x38) = &DAT_182be0a40;
  *(undefined8 *)((longlong)param_1 + 0x58) = param_5;
  *(undefined4 *)((longlong)param_1 + 0x50) = param_4;
  *(uint *)((longlong)param_1 + 100) = param_6 | 0x400000;
  *(undefined1 *)((longlong)param_1 + 0x68) = 0;
  *(undefined4 *)((longlong)param_1 + 0x30) = 0x11f;
  *(undefined4 *)((longlong)param_1 + 0x34) = 0xf;
  *(undefined4 *)((longlong)param_1 + 0x40) = 1;
  *(undefined4 *)((longlong)param_1 + 0x60) = 0;
  return param_1;
}

