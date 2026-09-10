
void FUN_18082e760(longlong param_1,longlong *param_2)

{
  undefined4 *puVar1;
  int iVar2;
  undefined4 *puVar3;
  longlong lVar4;
  undefined1 local_28 [32];
  
  lVar4 = 0;
  iVar2 = (**(code **)(*param_2 + 0x28))(param_2);
  if ((iVar2 == 0) || (iVar2 == 1)) {
    lVar4 = *(longlong *)(param_2[5] + 0x18);
  }
  puVar3 = (undefined4 *)(**(code **)(*param_2 + 0x70))(param_2,local_28);
  puVar1 = (undefined4 *)(param_1 + 0xa90);
  *puVar1 = *puVar3;
  *(undefined4 *)(param_1 + 0xa94) = puVar3[1];
  *(undefined4 *)(param_1 + 0xa98) = puVar3[2];
  *(undefined4 *)(param_1 + 0xa9c) = puVar3[3];
  *(undefined8 *)(param_1 + 0xaa0) = *(undefined8 *)(puVar3 + 4);
  *(undefined4 *)(param_1 + 0xaa8) = puVar3[6];
  *(undefined4 *)(param_1 + 0xaac) = puVar3[7];
  *(undefined4 *)(param_1 + 0xab0) = *(undefined4 *)(lVar4 + 0xab0);
  *(undefined4 *)(param_1 + 300) = *(undefined4 *)(lVar4 + 300);
  *(undefined4 *)(param_1 + 0x128) = *(undefined4 *)(lVar4 + 0x128);
  *(byte *)(param_1 + 0xa08) =
       *(byte *)(param_1 + 0xa08) ^ (*(byte *)(param_1 + 0xa08) ^ *(byte *)(lVar4 + 0xa08)) & 2;
  *(byte *)(param_1 + 0xa08) =
       (*(byte *)(lVar4 + 0xa08) ^ *(byte *)(param_1 + 0xa08)) & 1 ^ *(byte *)(param_1 + 0xa08);
  *(undefined4 *)(param_1 + 0xe0) = *(undefined4 *)(lVar4 + 0xe0);
  *(undefined4 *)(param_1 + 0x610) = 0;
  FUN_180839500(param_1 + 0x140,puVar1,1);
  FUN_180831aa0(param_1 + 0xac0,param_1 + 0x140,param_1 + 0x700,puVar1);
  return;
}


