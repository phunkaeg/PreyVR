
void Prey_CAttachmentBONEUpdateStatic(longlong *param_1,longlong *param_2)

{
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  float fVar10;
  float fVar11;
  float fVar12;
  float *pfVar13;
  undefined8 uVar14;
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
  
  if (-1 < *(int *)((longlong)param_1 + 0x15c)) {
    if ((*(uint *)(param_1 + 1) & 0x4000) == 0) {
      (**(code **)(*param_1 + 0x80))();
    }
    pfVar13 = (float *)(**(code **)(*param_2 + 0x48))
                                 (param_2,*(undefined4 *)((longlong)param_1 + 0x15c));
    fVar16 = *(float *)((longlong)param_1 + 0x10c);
    fVar1 = *(float *)(param_1 + 0x21);
    fVar2 = *(float *)(param_1 + 0x22);
    fVar3 = pfVar13[1];
    fVar25 = pfVar13[2];
    fVar4 = pfVar13[3];
    fVar5 = *pfVar13;
    fVar6 = *(float *)((longlong)param_1 + 0xfc);
    fVar19 = (fVar2 * fVar3 - fVar16 * fVar25) + fVar1 * fVar4;
    fVar20 = (fVar1 * fVar25 - fVar2 * fVar5) + fVar16 * fVar4;
    fVar18 = (fVar16 * fVar5 - fVar1 * fVar3) + fVar2 * fVar4;
    fVar17 = fVar18 * fVar3 - fVar20 * fVar25;
    fVar15 = fVar19 * fVar25 - fVar18 * fVar5;
    fVar21 = fVar20 * fVar5 - fVar19 * fVar3;
    fVar18 = *(float *)((longlong)param_1 + 0x104);
    fVar19 = *(float *)(param_1 + 0x1f);
    fVar20 = *(float *)(param_1 + 0x20);
    fVar7 = pfVar13[4];
    fVar8 = pfVar13[5];
    fVar9 = pfVar13[6];
    fVar24 = fVar18 * fVar4 - (fVar19 * fVar5 + fVar6 * fVar3 + fVar20 * fVar25);
    fVar10 = *(float *)(param_1 + 0x2b);
    pfVar13 = (float *)(param_1 + 0x26);
    fVar23 = (fVar20 * fVar3 - fVar6 * fVar25) + fVar19 * fVar4 + fVar18 * fVar5;
    fVar11 = *(float *)(param_1 + 0x2a);
    fVar12 = *(float *)((longlong)param_1 + 0x14c);
    fVar22 = (fVar19 * fVar25 - fVar20 * fVar5) + fVar6 * fVar4 + fVar18 * fVar3;
    fVar25 = (fVar6 * fVar5 - fVar19 * fVar3) + fVar20 * fVar4 + fVar18 * fVar25;
    fVar3 = *(float *)((longlong)param_1 + 0x154);
    *(float *)((longlong)param_1 + 0x13c) =
         fVar10 * fVar24 - (fVar12 * fVar23 + fVar11 * fVar22 + fVar3 * fVar25);
    *pfVar13 = (fVar3 * fVar22 - fVar11 * fVar25) + fVar12 * fVar24 + fVar10 * fVar23;
    *(float *)(param_1 + 0x27) =
         (fVar11 * fVar23 - fVar12 * fVar22) + fVar3 * fVar24 + fVar10 * fVar25;
    *(float *)((longlong)param_1 + 0x134) =
         (fVar12 * fVar25 - fVar3 * fVar23) + fVar11 * fVar24 + fVar10 * fVar22;
    param_1[0x28] = CONCAT44(fVar15 + fVar16 + fVar15 + fVar8,fVar17 + fVar1 + fVar17 + fVar7);
    *(float *)(param_1 + 0x29) = fVar21 + fVar2 + fVar21 + fVar9;
    if ((char)param_1[6] != '\0') {
      if ((char)param_1[6] == '\x04') {
        uVar14 = (**(code **)(*param_1 + 0x10))();
        Prey_CAttachmentBONESimulateEntityBinding(param_1 + 1,param_2,0xffffffff,pfVar13,uVar14);
      }
      else {
        uVar14 = (**(code **)(*param_1 + 0x10))(param_1);
        FUN_1807c0f00(param_1 + 1,param_2,0xffffffff,pfVar13,uVar14);
      }
    }
    (**(code **)(*(longlong *)param_1[4] + 0x10))((longlong *)param_1[4],param_1);
    *(uint *)(param_1 + 1) = *(uint *)(param_1 + 1) & 0xffffdfff;
    fVar16 = (float)(**(code **)(*(longlong *)param_1[4] + 0x38))();
    if ((fVar16 != 0.0) && (*(float *)(param_1[5] + 0x3c) <= fVar16)) {
      *(uint *)(param_1 + 1) = *(uint *)(param_1 + 1) | 0x2000;
    }
  }
  return;
}


