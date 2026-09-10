
void FUN_1816aab70(longlong param_1,float *param_2,float *param_3,undefined8 param_4,
                  undefined1 param_5,undefined8 param_6,undefined4 param_7)

{
  undefined4 uVar1;
  float fVar2;
  float fVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  float *pfVar7;
  longlong lVar8;
  int iVar9;
  int iVar10;
  float fVar11;
  undefined4 uVar12;
  float fVar13;
  float fVar14;
  undefined1 auVar15 [16];
  float fVar16;
  float fVar17;
  float fVar18;
  float fVar19;
  float fVar20;
  undefined4 local_138;
  int local_134;
  undefined8 local_130;
  float local_128;
  float local_118;
  float local_114;
  float local_110;
  undefined8 local_108;
  undefined1 *local_100;
  undefined4 *local_f8;
  longlong local_f0;
  float *local_e8;
  float *local_e0;
  undefined1 local_d8 [176];
  
  iVar4 = FUN_181695070(param_1,&DAT_182bd70c8);
  local_134 = iVar4;
  iVar5 = FUN_181695070(param_1,&DAT_182bd70c0);
  if (0 < iVar5 * iVar4) {
    local_138 = FUN_181695020(param_1,&DAT_182bd70e8);
    local_100 = (undefined1 *)&param_6;
    local_f8 = &local_138;
    local_108 = param_4;
    local_f0 = param_1;
    local_e8 = param_2;
    local_e0 = param_3;
    FUN_1816a7050(&local_108,&local_130);
    fVar2 = (float)local_130;
    fVar3 = local_130._4_4_;
    fVar11 = (float)FUN_1816af3f0(local_138,
                                  SQRT((param_2[1] - local_130._4_4_) *
                                       (param_2[1] - local_130._4_4_) +
                                       (*param_2 - (float)local_130) * (*param_2 - (float)local_130)
                                       + (param_2[2] - local_128) * (param_2[2] - local_128)));
    uVar1 = param_7;
    fVar13 = param_3[2];
    fVar20 = param_3[1];
    fVar18 = *param_3;
    fVar19 = fVar11 * param_3[3] - fVar20 * fVar11;
    fVar14 = fVar11 * param_3[3] + fVar20 * fVar11;
    fVar17 = fVar18 * fVar11 - fVar13 * fVar11;
    fVar16 = fVar13 * fVar17 - fVar20 * fVar14;
    fVar13 = fVar18 * fVar14 - fVar13 * fVar19;
    fVar20 = fVar20 * fVar19 - fVar18 * fVar17;
    iVar10 = 0;
    iVar6 = local_134;
    if (0 < iVar4) {
      do {
        iVar9 = 0;
        if (0 < iVar5) {
          do {
            if ((iVar10 != 0) || (fVar18 = fVar2, fVar14 = fVar3, fVar17 = local_128, iVar9 != 0)) {
              pfVar7 = (float *)FUN_1816ad190(local_d8,*(undefined8 *)(param_1 + 0x40),param_3,
                                              (fVar11 * 2.0) / (float)iVar5,
                                              (fVar11 * 2.0) / (float)iVar4,iVar9,iVar10);
              fVar18 = (fVar2 - (fVar16 + fVar16 + fVar11)) + *pfVar7;
              fVar14 = (fVar3 - (fVar13 + fVar13)) + pfVar7[1];
              fVar17 = (local_128 - (fVar20 + fVar20 + fVar11)) + pfVar7[2];
            }
            fVar18 = fVar18 - *param_2;
            fVar14 = fVar14 - param_2[1];
            fVar17 = fVar17 - param_2[2];
            local_130 = *(undefined1 **)(param_1 + 0x2e0);
            fVar19 = fVar18 * fVar18 + fVar14 * fVar14 + fVar17 * fVar17 + 1.1754944e-38;
            auVar15 = rsqrtss(ZEXT416((uint)fVar19),ZEXT416((uint)fVar19));
            local_110 = auVar15._0_4_;
            local_110 = (1.5 - local_110 * fVar19 * local_110 * 0.5) * local_110;
            local_118 = fVar18 * local_110;
            local_114 = fVar14 * local_110;
            local_110 = fVar17 * local_110;
            if (*(int *)(local_130 + -0xc) < 0) {
              local_130 = &DAT_18224d03c;
            }
            else {
              *(int *)(local_130 + -0xc) = *(int *)(local_130 + -0xc) + 1;
            }
            lVar8 = FUN_181677040(*(undefined8 *)(param_1 + 0x4d8),param_2,&local_118,
                                  *(undefined4 *)(param_1 + 0x38),&local_130,param_5,
                                  *(undefined1 *)(param_1 + 0x2c8),0x20,uVar1,0);
            if (lVar8 != 0) {
              uVar12 = FUN_181695020(param_1,&DAT_182bd6ee8);
              FUN_1816768a0(lVar8,uVar12);
            }
            iVar9 = iVar9 + 1;
            iVar6 = local_134;
          } while (iVar9 < iVar5);
        }
        iVar10 = iVar10 + 1;
      } while (iVar10 < iVar6);
    }
  }
  return;
}


