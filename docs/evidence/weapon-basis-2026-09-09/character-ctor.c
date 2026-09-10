
undefined8 * FUN_18082bd50(undefined8 *param_1,undefined8 param_2,undefined8 param_3)

{
  uint uVar1;
  longlong lVar2;
  int iVar3;
  int iVar4;
  longlong lVar5;
  int local_res8 [2];
  
  *param_1 = &PTR_FUN_181d22200;
  param_1[1] = 0;
  param_1[2] = 0;
  param_1[3] = &PTR_FUN_181d22110;
  param_1[5] = 0;
  FUN_18082b810(param_1 + 7);
  FUN_18082b890(param_1 + 8);
  FUN_1808304e0(param_1 + 9);
  param_1[6] = 0;
  param_1[0xb] = 0;
  *(undefined4 *)((longlong)param_1 + 0x54) = 0;
  lVar2 = param_1[7];
  uVar1 = *(uint *)(lVar2 + -4);
  iVar4 = uVar1 * 8;
  if ((uVar1 & 0x80000000) == 0) {
    iVar3 = (int)((ulonglong)(longlong)iVar4 >> 3);
  }
  else {
    iVar3 = *(int *)(iVar4 + lVar2);
  }
  if (0x1f < iVar3) goto LAB_18082be96;
  if ((uVar1 & 0x80000000) == 0) {
    iVar4 = (int)((ulonglong)(longlong)iVar4 >> 3);
  }
  else {
    iVar4 = *(int *)(iVar4 + lVar2);
  }
  if (iVar4 == 0x20) {
LAB_18082be5e:
    lVar2 = param_1[7];
    *(undefined4 *)(lVar2 + -4) = 0x20;
    if (0x103 < (ulonglong)((longlong)iVar4 << 3)) {
      *(undefined4 *)(lVar2 + -4) = 0x80000020;
      *(int *)(lVar2 + 0x100) = iVar4;
    }
  }
  else {
    local_res8[0] = 0x20;
    lVar5 = 0;
    if (uVar1 != 0) {
      lVar5 = lVar2;
    }
    lVar2 = FUN_18081d810(param_1 + 7,lVar5,uVar1 & 0x7fffffff,local_res8,8,0);
    param_1[7] = lVar2;
    iVar4 = local_res8[0];
    if (lVar2 != 0) goto LAB_18082be5e;
    FUN_18082b810(param_1 + 7);
  }
  FUN_18082b5b0(param_1 + 7,uVar1 & 0x7fffffff,1);
LAB_18082be96:
  *(undefined4 *)(param_1 + 4) = 0;
  *(undefined4 *)(param_1 + 10) = 0;
  param_1[0xc] = 0;
  param_1[0xd] = 0;
  param_1[0xe] = 0;
  param_1[0xf] = 0;
  param_1[0x10] = 0;
  param_1[0x19] = &DAT_18224d03c;
  param_1[0x1e] = 0;
  param_1[0x1f] = 0;
  param_1[0x20] = 0;
  param_1[0x21] = 0;
  param_1[0x22] = 0;
  param_1[0x23] = 0;
  FUN_180837ec0(param_1 + 0x28);
  FUN_1808327f0(param_1 + 0xe0);
  FUN_180830f70(param_1 + 0x158);
  param_1[0x15a] = 0;
  param_1[0x15b] = 0;
  *(undefined1 *)(param_1 + 0x15c) = 1;
  FUN_180125390(param_1 + 0x15d);
  FUN_18088e6c0(DAT_18276e308,param_3,param_1);
  FUN_18082c750(param_1 + 2,param_3);
  FUN_1800944d0(param_1 + 0x19,param_2);
  *(uint *)(param_1 + 0x26) = *(uint *)(param_1 + 0x26) | 1;
  *(uint *)(param_1 + 0x1d) = *(uint *)(param_1 + 0x1d) & 0xfffffffb;
  *(undefined1 *)((longlong)param_1 + 0xacc) = 0;
  *(undefined4 *)(param_1 + 0x12) = 0;
  *(undefined4 *)(param_1 + 0x1a) = 0;
  *(undefined8 *)((longlong)param_1 + 0xdc) = 0x55aa55aa;
  *(undefined4 *)((longlong)param_1 + 0xd4) = 0xffaaffaa;
  *(undefined4 *)(param_1 + 0x1b) = 0;
  param_1[0x25] = 0;
  *(undefined8 *)((longlong)param_1 + 0xa84) = 0x3f80000000000000;
  *(undefined4 *)((longlong)param_1 + 0xa8c) = 0;
  *(undefined4 *)((longlong)param_1 + 0xa9c) = 0x3f800000;
  param_1[0x152] = 0;
  *(undefined4 *)(param_1 + 0x153) = 0;
  *(undefined8 *)((longlong)param_1 + 0xaac) = 0x3f800000;
  param_1[0x154] = 0;
  *(undefined4 *)(param_1 + 0x155) = 0;
  *(uint *)(param_1 + 0x26) = *(uint *)(param_1 + 0x26) & 0xfffffffd;
  *(uint *)(param_1 + 0x1d) = *(uint *)(param_1 + 0x1d) & 0xfffffffc;
  param_1[6] = param_1;
  *(undefined4 *)((longlong)param_1 + 0x124) = 0;
  *(undefined1 *)(param_1 + 0x24) = 1;
  *(undefined4 *)(param_1 + 0x150) = 0x3f800000;
  *(undefined8 *)((longlong)param_1 + 0xab4) = 0xffffffffffffffff;
  *(undefined4 *)(param_1 + 0x159) = 1;
  *(undefined1 *)((longlong)param_1 + 0xabc) = 0;
  param_1[0x13] = 0;
  param_1[0x14] = 0;
  param_1[0x15] = 0;
  param_1[0x16] = 0;
  param_1[0x17] = 0;
  param_1[0x18] = 0;
  FUN_180838e40(param_1 + 0x28,param_1,param_1 + 0xe0);
  FUN_18082e8e0(param_1,0);
  return param_1;
}


