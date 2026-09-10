
undefined8 * FUN_180def000(undefined8 *param_1)

{
  undefined8 *puVar1;
  char *pcVar2;
  undefined8 uVar3;
  longlong lVar4;
  longlong *plVar5;
  longlong *plVar6;
  
  *param_1 = &PTR_FUN_181d9b9c8;
  param_1[1] = &PTR_LAB_181d9bfd8;
  param_1[2] = &PTR_LAB_181d9bff0;
  *(undefined1 *)(param_1 + 4) = 0;
  *(undefined4 *)((longlong)param_1 + 0x24) = 0;
  param_1[3] = &PTR_FUN_181d9b9b0;
  SteamAPI_RegisterCallback(param_1 + 3,0x14b);
  param_1[5] = &DAT_18224d980;
  FUN_180e142e0(param_1 + 6);
  FUN_1801109c0(param_1 + 0xf1);
  param_1[0x13f] = &DAT_18224d03c;
  param_1[0x142] = &DAT_18224d03c;
  param_1[0x153] = 0;
  param_1[0x154] = 0;
  uVar3 = FUN_180df72d0(param_1 + 0x153);
  param_1[0x153] = uVar3;
  FUN_1801109c0(param_1 + 0x159);
  param_1[0x1a3] = 0;
  param_1[0x1ac] = &DAT_18224d03c;
  param_1[0x1ec] = &DAT_18224d03c;
  param_1[0x1f2] = 0;
  param_1[499] = 0;
  param_1[500] = 0;
  param_1[0x1f7] = &PTR_FUN_181d9b718;
  param_1[0x1fa] = 0;
  param_1[0x1fd] = 0;
  param_1[0x1ff] = 0;
  param_1[0x1fe] = 0;
  param_1[0x201] = 0;
  param_1[0x200] = 0;
  param_1[0x203] = 0;
  param_1[0x202] = 0;
  FUN_180deea70(param_1 + 0x204);
  param_1[0x218] = &PTR_FUN_181d9b6c8;
  *(undefined4 *)(param_1 + 0x219) = 0;
  param_1[0x21a] = 0;
  param_1[0x21b] = 0;
  uVar3 = FUN_180df7380(param_1 + 0x21a,0,0);
  param_1[0x21a] = uVar3;
  *(undefined4 *)(param_1 + 0x219) = 0x3f800000;
  param_1[0x21c] = 0;
  param_1[0x21d] = 0;
  param_1[0x21e] = 0;
  FUN_180df7640(param_1 + 0x219,8);
  param_1[0x225] = 0;
  param_1[0x226] = &PTR_FUN_181d9b980;
  param_1[0x5ab] = 0;
  param_1[0x5ac] = 0;
  param_1[0x5ad] = 0;
  param_1[0x5b6] = &DAT_18224d03c;
  param_1[0x5b7] = &DAT_18224d03c;
  param_1[0x5b8] = 0;
  param_1[0x5b9] = 0;
  param_1[0x5ba] = 0;
  *(undefined4 *)(param_1 + 0x5bb) = 0xffffffff;
  param_1[0x5bc] = 0;
  param_1[0x5bd] = 0;
  uVar3 = FUN_180df73d0(param_1 + 0x5bc,0,0);
  param_1[0x5bc] = uVar3;
  puVar1 = param_1 + 0x5c0;
  *(undefined4 *)puVar1 = 0;
  param_1[0x5c1] = 0;
  param_1[0x5c2] = 0;
  uVar3 = FUN_180df7320(param_1 + 0x5c1,0,0);
  param_1[0x5c1] = uVar3;
  *(undefined4 *)puVar1 = 0x3f800000;
  param_1[0x5c3] = 0;
  param_1[0x5c4] = 0;
  param_1[0x5c5] = 0;
  FUN_180df77e0(puVar1);
  param_1[0x5c8] = &DAT_18224d03c;
  plVar6 = (longlong *)0x0;
  *(undefined4 *)(param_1 + 0x5ae) = 0;
  *(undefined8 *)((longlong)param_1 + 0xd6c) = 0;
  *(undefined4 *)((longlong)param_1 + 0xd74) = 0;
  *(undefined1 *)(param_1 + 0x5bf) = 0;
  lVar4 = FUN_181b77290(0x98);
  plVar5 = plVar6;
  if (lVar4 != 0) {
    plVar5 = (longlong *)FUN_180df9ec0(lVar4);
  }
  param_1[0x1a4] = plVar5;
  if (plVar5 != (longlong *)0x0) {
    (**(code **)(*plVar5 + 8))();
  }
  param_1[0x1f1] = 0;
  param_1[0x1f0] = 0;
  memset((void *)param_1[5],0,0x300);
  memset(param_1 + 0x144,0,0x78);
  *(undefined8 **)(param_1[5] + 0xe0) = param_1;
  *(undefined8 **)(param_1[5] + 0x98) = param_1 + 6;
  *(undefined8 **)(param_1[5] + 0x110) = param_1 + 0x218;
  *(undefined8 **)(param_1[5] + 0x90) = param_1 + 0x1f7;
  *(undefined1 *)(param_1[5] + 0x200) = 0;
  *(undefined1 *)(param_1[5] + 0x201) = 0;
  *(undefined1 *)(param_1[5] + 0x202) = 0;
  *(undefined1 *)(param_1[5] + 0x203) = 0;
  *(undefined8 *)(param_1[5] + 0x208) = 0;
  *(undefined8 *)(param_1[5] + 0x210) = 0;
  *(undefined1 *)(param_1[5] + 0x242) = 0;
  *(undefined1 *)(param_1[5] + 0x243) = 0;
  *(undefined1 *)(param_1[5] + 0x244) = 0;
  *(undefined4 *)(param_1[5] + 0x168) = 0;
  *(undefined1 *)(param_1[5] + 0x2ab) = 0;
  *(undefined1 *)(param_1[5] + 0x2ac) = 0;
  *(undefined1 *)(param_1[5] + 0x180) = 0;
  *(undefined1 *)(param_1[5] + 0x2aa) = 0;
  param_1[0x1ef] = &PTR_PTR_1822770c8;
  param_1[0x155] = 0;
  param_1[0x221] = 0;
  param_1[0x222] = 0;
  param_1[0x223] = 0;
  param_1[0x1a5] = 0;
  param_1[0x5b1] = 0;
  param_1[0x1b6] = 0;
  param_1[0x1b7] = 0;
  param_1[0x1b8] = 0;
  param_1[0x1b9] = 0;
  param_1[0x1ca] = 0;
  param_1[0x1ba] = 0;
  param_1[0x1bb] = 0;
  param_1[0x1bc] = 0;
  param_1[0x1c2] = 0;
  param_1[0x157] = 0;
  param_1[0x156] = 0;
  param_1[0x13b] = 0;
  param_1[0x5b0] = 0;
  param_1[0x13d] = 0;
  param_1[0x1a6] = 0;
  param_1[0x1a7] = 0;
  param_1[0x1a8] = 0;
  param_1[0x1a9] = 0;
  param_1[0x217] = 0;
  param_1[0x1e4] = 0;
  param_1[0x1e5] = 0;
  param_1[0x5b5] = 0;
  param_1[0x1b5] = 0;
  param_1[0x1ed] = 0;
  param_1[0x1ee] = 0;
  param_1[0x1e9] = 0;
  param_1[0x1c9] = 0;
  param_1[0x1c8] = 0;
  param_1[0x1ce] = 0;
  param_1[0x1cf] = 0;
  param_1[0x1d1] = 0;
  param_1[0x1d2] = 0;
  param_1[0x1d0] = 0;
  param_1[0x1d3] = 0;
  param_1[0x1d4] = 0;
  param_1[0x1d5] = 0;
  param_1[0x1d6] = 0;
  param_1[0x1d7] = 0;
  param_1[0x1d8] = 0;
  param_1[0x1d9] = 0;
  param_1[0x1da] = 0;
  param_1[0x1db] = 0;
  param_1[0x1dc] = 0;
  param_1[0x1dd] = 0;
  param_1[0x1de] = 0;
  param_1[0x1df] = 0;
  param_1[0x1e0] = 0;
  param_1[0x1e1] = 0;
  param_1[0x1e6] = 0;
  param_1[0x1e7] = 0;
  param_1[0x1e8] = 0;
  param_1[0x1ea] = 0;
  param_1[0x1eb] = 0;
  param_1[0x140] = 0;
  param_1[0x1b1] = 0;
  *(undefined1 *)(param_1 + 0x139) = 0;
  *(undefined2 *)((longlong)param_1 + 0x9c9) = 0;
  *(undefined4 *)((longlong)param_1 + 0x9cc) = 0;
  *(undefined2 *)(param_1 + 0x13a) = 0;
  *(undefined2 *)((longlong)param_1 + 0x9d3) = 0;
  *(undefined1 *)((longlong)param_1 + 0x9d7) = 0;
  *(undefined1 *)((longlong)param_1 + 0x9d2) = 0;
  lVar4 = -1;
  do {
    pcVar2 = &DAT_181c7ce7c + lVar4;
    lVar4 = lVar4 + 1;
  } while (*pcVar2 != '\0');
  FUN_180095680(param_1 + 0x5c8);
  *(undefined4 *)(param_1 + 0x13e) = 1000;
  param_1[0x1f5] = 0;
  param_1[0x1f6] = 0;
  param_1[0x1c4] = 0;
  param_1[0x5aa] = 0;
  param_1[0x216] = 0;
  lVar4 = FUN_181b77290(0x9f8);
  plVar5 = plVar6;
  if (lVar4 != 0) {
    plVar5 = (longlong *)FUN_180e79960(lVar4);
  }
  param_1[0x216] = plVar5;
  *(undefined4 *)(param_1 + 0x13c) = 0;
  *(undefined4 *)(param_1 + 0x224) = 4;
  *(undefined4 *)((longlong)param_1 + 0x1124) = 6;
  param_1[0x5af] = 0;
  *(undefined1 *)((longlong)param_1 + 0x2d3c) = 0;
  *(undefined1 *)((longlong)param_1 + 0x2d3e) = 0;
  param_1[0x5a8] = 0;
  *(undefined4 *)(param_1 + 0x1ad) = 0xffffffff;
  param_1[0x158] = 0;
  lVar4 = FUN_181b77290(0x30);
  plVar5 = plVar6;
  if (lVar4 != 0) {
    plVar5 = (longlong *)FUN_180e48fb0(lVar4,param_1);
  }
  param_1[0x1aa] = plVar5;
  uVar3 = FUN_180dbf2c0();
  param_1[0x1ab] = uVar3;
  lVar4 = FUN_181b77290(0x28);
  plVar5 = plVar6;
  if (lVar4 != 0) {
    plVar5 = (longlong *)FUN_180e92530(lVar4);
  }
  param_1[0x5b1] = plVar5;
  uVar3 = FUN_180099890();
  param_1[0x157] = uVar3;
  lVar4 = FUN_181b77290(0x180);
  plVar5 = plVar6;
  if (lVar4 != 0) {
    plVar5 = (longlong *)FUN_180e96480(lVar4);
  }
  param_1[0x5b2] = plVar5;
  lVar4 = FUN_181b77290(0x2e0);
  if (lVar4 != 0) {
    plVar6 = (longlong *)FUN_180de8100(lVar4);
  }
  param_1[0x5b3] = plVar6;
  param_1[0x5b4] = 0;
  param_1[0x1f8] = 0;
  param_1[0x1f9] = 0;
  param_1[0x1fc] = 0;
  param_1[0x1fb] = 0;
  lVar4 = FUN_181b77290(0x38);
  if (lVar4 == 0) {
    DAT_182b13540 = 0;
  }
  else {
    DAT_182b13540 = FUN_180e2ac30(lVar4);
  }
  *(undefined1 *)((longlong)param_1 + 0x9d5) = 0;
  *(undefined1 *)((longlong)param_1 + 0x2d3d) = 0;
  uVar3 = FUN_180dd5740();
  *(undefined8 *)(param_1[5] + 0x140) = uVar3;
  *(undefined4 *)(param_1 + 0x5a7) = 0;
  *(undefined2 *)(param_1 + 0x5be) = 0;
  *(undefined1 *)((longlong)param_1 + 0x2d3d) = 0;
  *(undefined4 *)((longlong)param_1 + 0x2df4) = 0x1d;
  *(undefined1 *)((longlong)param_1 + 0x2df9) = 0;
  return param_1;
}

