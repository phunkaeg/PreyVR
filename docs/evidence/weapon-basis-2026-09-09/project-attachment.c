
bool FUN_1807a2560(longlong param_1)

{
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  longlong *plVar5;
  longlong lVar6;
  int iVar7;
  int iVar8;
  float *pfVar9;
  float fVar10;
  float fVar11;
  float fVar12;
  float fVar13;
  float fVar14;
  float fVar15;
  float fVar16;
  float fVar17;
  float fVar18;
  float fVar19;
  float fVar20;
  float fVar21;
  float fVar22;
  float fVar23;
  float fVar24;
  float fVar25;
  
  *(undefined4 *)(param_1 + 0x15c) = 0xffffffff;
  plVar5 = *(longlong **)(*(longlong *)(*(longlong *)(param_1 + 0x28) + 0x18) + 0x10);
  iVar7 = (**(code **)(*plVar5 + 0x38))(plVar5,*(undefined8 *)(param_1 + 0x160));
  if (-1 < iVar7) {
    lVar6 = *(longlong *)(*(longlong *)(param_1 + 0x28) + 0x18);
    iVar8 = (**(code **)(**(longlong **)(lVar6 + 0x10) + 8))();
    if (iVar7 < iVar8) {
      *(int *)(param_1 + 0x15c) = iVar7;
      plVar5 = *(longlong **)(lVar6 + 0x10);
      pfVar9 = (float *)(**(code **)(*plVar5 + 0x48))(plVar5,iVar7);
      fVar1 = *pfVar9;
      fVar2 = pfVar9[1];
      fVar13 = pfVar9[2];
      fVar24 = -fVar1;
      fVar3 = pfVar9[3];
      fVar16 = -pfVar9[5];
      fVar17 = -pfVar9[6];
      fVar14 = -pfVar9[4];
      fVar23 = -fVar2;
      fVar25 = -fVar13;
      fVar18 = (fVar13 * fVar16 - fVar2 * fVar17) + fVar3 * fVar14;
      fVar12 = (fVar1 * fVar17 - fVar13 * fVar14) + fVar3 * fVar16;
      fVar21 = (fVar2 * fVar14 - fVar1 * fVar16) + fVar3 * fVar17;
      fVar20 = fVar13 * fVar12 - fVar21 * fVar2;
      fVar4 = *(float *)(param_1 + 0x128);
      fVar22 = fVar21 * fVar1 - fVar18 * fVar13;
      fVar19 = fVar18 * fVar2 - fVar1 * fVar12;
      fVar1 = *(float *)(param_1 + 300);
      fVar2 = *(float *)(param_1 + 0x124);
      fVar18 = (fVar1 * fVar23 - fVar4 * fVar25) + fVar2 * fVar3;
      fVar12 = (fVar2 * fVar25 - fVar1 * fVar24) + fVar4 * fVar3;
      fVar13 = (fVar4 * fVar24 - fVar2 * fVar23) + fVar1 * fVar3;
      fVar11 = fVar18 * fVar25 - fVar13 * fVar24;
      fVar15 = fVar13 * fVar23 - fVar25 * fVar12;
      fVar13 = *(float *)(param_1 + 0x114);
      fVar10 = fVar24 * fVar12 - fVar18 * fVar23;
      fVar12 = *(float *)(param_1 + 0x120);
      fVar18 = *(float *)(param_1 + 0x11c);
      fVar21 = *(float *)(param_1 + 0x118);
      *(float *)(param_1 + 0x104) =
           fVar12 * fVar3 - (fVar13 * fVar24 + fVar21 * fVar23 + fVar18 * fVar25);
      *(float *)(param_1 + 0xf8) =
           (fVar18 * fVar23 - fVar21 * fVar25) + fVar13 * fVar3 + fVar12 * fVar24;
      *(float *)(param_1 + 0xfc) =
           (fVar13 * fVar25 - fVar18 * fVar24) + fVar21 * fVar3 + fVar12 * fVar23;
      *(float *)(param_1 + 0x100) =
           (fVar21 * fVar24 - fVar13 * fVar23) + fVar18 * fVar3 + fVar12 * fVar25;
      *(ulonglong *)(param_1 + 0x108) =
           CONCAT44(fVar22 + fVar22 + fVar16 + fVar11 + fVar4 + fVar11,
                    fVar20 + fVar20 + fVar14 + fVar15 + fVar2 + fVar15);
      *(float *)(param_1 + 0x110) = fVar10 + fVar1 + fVar10 + fVar19 + fVar19 + fVar17;
      *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) | 0x4000;
    }
    return iVar7 < iVar8;
  }
  return false;
}


