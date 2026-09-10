
void * FUN_18124ba90(void *param_1,undefined8 *param_2,undefined8 *param_3,undefined8 param_4,
                    undefined4 param_5,undefined8 param_6)

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
  *(undefined4 *)((longlong)param_1 + 0x50) = param_5;
  *(undefined8 *)((longlong)param_1 + 0x58) = param_6;
  *(undefined8 *)((longlong)param_1 + 0x38) = param_4;
  *(undefined1 *)((longlong)param_1 + 0x68) = 0;
  *(undefined4 *)((longlong)param_1 + 0x30) = 0x117;
  *(undefined4 *)((longlong)param_1 + 0x34) = 0xf;
  *(undefined4 *)((longlong)param_1 + 0x40) = 1;
  *(undefined4 *)((longlong)param_1 + 0x60) = 0;
  *(undefined4 *)((longlong)param_1 + 100) = 0x400000;
  return param_1;
}

