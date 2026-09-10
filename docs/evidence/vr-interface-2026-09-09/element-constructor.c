
undefined8 *
FUN_1802f79b0(undefined8 *param_1,undefined8 param_2,longlong param_3,undefined4 param_4)

{
  longlong *plVar1;
  undefined8 uVar2;
  undefined8 *local_res8;
  
  param_1[5] = param_2;
  *param_1 = &PTR_FUN_181cab358;
  param_1[1] = &PTR_FUN_181cab6d0;
  param_1[2] = &PTR_LAB_181cab6e0;
  param_1[3] = &PTR_LAB_181cab6f8;
  *(undefined4 *)(param_1 + 4) = 1;
  param_1[6] = &DAT_18224d03c;
  param_1[7] = &DAT_18224d03c;
  param_1[8] = &DAT_18224d03c;
  param_1[9] = 0;
  *(undefined4 *)(param_1 + 10) = 0;
  param_1[0xb] = 0;
  param_1[0xc] = 0;
  *(undefined1 *)(param_1 + 0xe) = 0;
  param_1[0xf] = 0;
  *(undefined2 *)(param_1 + 0x10) = 0;
  *(undefined1 *)((longlong)param_1 + 0x82) = 0;
  *(undefined8 *)((longlong)param_1 + 0x84) = 2;
  *(undefined4 *)((longlong)param_1 + 0x8c) = 0;
  *(undefined4 *)(param_1 + 0x12) = 0x400;
  *(undefined4 *)((longlong)param_1 + 0x94) = 0x300;
  *(undefined4 *)(param_1 + 0x13) = 1;
  *(undefined4 *)((longlong)param_1 + 0x9c) = 1;
  *(undefined1 *)(param_1 + 0x14) = 1;
  param_1[0x15] = 0;
  param_1[0x16] = 0;
  param_1[0x17] = 0;
  uVar2 = FUN_180211fb0(param_1 + 0x16);
  param_1[0x16] = uVar2;
  param_1[0x18] = param_3;
  *(undefined4 *)(param_1 + 0x19) = param_4;
  param_1[0x1a] = 0;
  param_1[0x1b] = 0;
  param_1[0x1c] = 0;
  FUN_180303920(param_1 + 0x1d);
  param_1[0x1e] = 0;
  param_1[0x1f] = 0;
  uVar2 = FUN_1802d90a0(param_1 + 0x1e);
  param_1[0x1e] = uVar2;
  FUN_180303920(param_1 + 0x20);
  param_1[0x21] = 0;
  param_1[0x22] = 0;
  uVar2 = FUN_1802d90a0(param_1 + 0x21);
  param_1[0x21] = uVar2;
  FUN_1803038a0(param_1 + 0x23);
  param_1[0x24] = 0;
  param_1[0x25] = 0;
  uVar2 = FUN_1802d90a0(param_1 + 0x24);
  param_1[0x24] = uVar2;
  FUN_1803038a0(param_1 + 0x26);
  param_1[0x27] = 0;
  param_1[0x28] = 0;
  uVar2 = FUN_1802d90a0(param_1 + 0x27);
  param_1[0x27] = uVar2;
  FUN_1802d9e40(param_1 + 0x29);
  param_1[0x2a] = 0;
  param_1[0x2b] = 0;
  uVar2 = FUN_1802d90a0(param_1 + 0x2a);
  param_1[0x2a] = uVar2;
  FUN_1802d9e40(param_1 + 0x2c);
  param_1[0x2d] = 0;
  param_1[0x2e] = 0;
  uVar2 = FUN_1802d90a0(param_1 + 0x2d);
  param_1[0x2d] = uVar2;
  plVar1 = param_1 + 0x33;
  param_1[0x2f] = 0;
  param_1[0x30] = 0;
  param_1[0x31] = 0;
  *(undefined4 *)(param_1 + 0x32) = 0;
  *plVar1 = 0;
  param_1[0x34] = 0;
  param_1[0x35] = 0;
  param_1[0x36] = 0;
  *(undefined2 *)(param_1 + 0x37) = 0;
  if ((ulonglong)(param_1[0x35] - *plVar1 >> 3) < 4) {
    FUN_180302620(plVar1,4);
  }
  param_1[0x38] = 0;
  param_1[0x39] = 0;
  uVar2 = FUN_180302310(param_1 + 0x38);
  param_1[0x38] = uVar2;
  param_1[0x3a] = 0;
  param_1[0x3b] = 0;
  uVar2 = FUN_1803022c0(param_1 + 0x3a);
  param_1[0x3a] = uVar2;
  *(undefined4 *)(param_1 + 0x3c) = 0x10000;
  *(undefined1 *)((longlong)param_1 + 0x1e4) = 0;
  param_1[0x3d] = 0x3f800000;
  *(undefined4 *)(param_1 + 0x3e) = 0x41a00000;
  param_1[0x3f] = 0;
  param_1[0x40] = 0xffffffffffffffff;
  param_1[0x41] = 0;
  param_1[0x42] = 0;
  param_1[0x43] = 0;
  FUN_180098690(param_1 + 0x44);
  param_1[0x49] = 0;
  param_1[0x4a] = 0;
  param_1[0x4b] = 0;
  if (param_3 == 0) {
    local_res8 = param_1;
    FUN_1802f6600(param_1 + 0x1a,&local_res8);
  }
  return param_1;
}

