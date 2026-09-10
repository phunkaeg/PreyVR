
ulonglong FUN_1808bc240(longlong param_1,byte *param_2)

{
  byte bVar1;
  longlong lVar2;
  byte bVar3;
  ushort uVar4;
  uint uVar6;
  ulonglong uVar5;
  
  uVar6 = 0xffffffff;
  bVar1 = *param_2;
  while (bVar1 != 0) {
    bVar1 = *param_2;
    param_2 = param_2 + 1;
    bVar3 = bVar1 + 0x20;
    if (0x19 < (byte)(bVar1 + 0xbf)) {
      bVar3 = bVar1;
    }
    uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar3 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
            uVar6 >> 8;
    bVar1 = *param_2;
  }
  lVar2 = *(longlong *)(param_1 + 0x10);
  if ((lVar2 != 0) &&
     (uVar4 = *(ushort *)(lVar2 + (ulonglong)(~uVar6 * 0xd - 0xa1 & 0x1ff) * 2), uVar4 != 0x3ff)) {
    lVar2 = *(longlong *)(lVar2 + 0x400);
    do {
      uVar5 = (ulonglong)uVar4;
      if (*(uint *)(lVar2 + 4 + uVar5 * 0xc) == ~uVar6) {
        return (ulonglong)*(ushort *)(lVar2 + uVar5 * 0xc);
      }
      uVar4 = *(ushort *)(lVar2 + 8 + uVar5 * 0xc);
    } while (uVar4 != 0x3ff);
  }
  return 0xffffffff;
}

