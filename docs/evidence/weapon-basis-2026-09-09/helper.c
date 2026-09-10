
float * Prey_GetEntitySlotHelperWorldTM
                  (float *param_1,longlong *param_2,undefined4 param_3,undefined8 param_4,
                  char param_5,char param_6)

{
  int *piVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  float fVar10;
  char cVar11;
  int iVar12;
  float *pfVar13;
  longlong *plVar14;
  undefined8 *puVar15;
  longlong *plVar16;
  float fVar17;
  float fVar18;
  float fVar19;
  float fVar20;
  float fVar21;
  float fVar22;
  float fVar23;
  float fVar24;
  float fVar25;
  float fVar26;
  float fVar27;
  float local_res10;
  float local_1f8;
  float local_1f4;
  float local_1f0;
  float local_1ec;
  float local_1e8;
  float local_1e4;
  float local_1d4;
  float local_1d0;
  float local_1cc;
  float local_1c8;
  float local_1c4;
  float local_1c0;
  float local_1bc;
  float local_1b8;
  float local_1b4;
  float local_1b0;
  float local_1ac;
  float local_1a8;
  float local_1a4;
  float local_1a0;
  float local_19c;
  float local_198;
  float local_194;
  float local_190;
  float local_18c;
  float local_188;
  float local_184;
  float local_180;
  undefined8 local_178;
  undefined8 uStack_170;
  undefined8 local_168;
  undefined4 local_160;
  undefined4 local_15c;
  float local_158;
  float local_154;
  float local_150;
  float local_14c;
  float local_148;
  float local_144;
  float local_140;
  float local_13c;
  undefined1 local_138 [8];
  longlong local_130;
  int local_128;
  longlong *local_108;
  longlong *local_100;
  
  local_130 = 0;
  local_128 = 0;
  if (param_2 == (longlong *)0x0) {
    local_1f4 = local_1a8;
LAB_1811a66c7:
    local_1f8 = local_1b0;
    local_1ec = local_1b8;
    local_1f0 = local_1bc;
    local_1e8 = local_1c8;
    local_1e4 = local_1cc;
    local_1d4 = local_1d0;
    fVar19 = local_1c4;
    fVar27 = local_1b4;
    local_res10 = local_1ac;
LAB_1811a6733:
    fVar22 = local_1ec;
    fVar17 = local_1e8;
    fVar26 = local_1c0;
    fVar20 = local_1d4;
    if (param_5 != '\0') {
      pfVar13 = (float *)(**(code **)(*param_2 + 0xe8))(param_2);
      fVar22 = pfVar13[1];
      fVar2 = *pfVar13;
      fVar3 = pfVar13[2];
      fVar24 = pfVar13[4];
      fVar4 = pfVar13[5];
      fVar5 = pfVar13[9];
      fVar6 = pfVar13[10];
      fVar25 = pfVar13[6];
      fVar23 = pfVar13[8];
      fVar20 = fVar22 * local_1c0 + fVar2 * local_1d4 + fVar3 * local_1f8;
      fVar26 = fVar4 * local_1c0 + fVar24 * local_1d4 + fVar25 * local_1f8;
      fVar21 = fVar5 * local_1f0;
      local_1f8 = fVar5 * local_1c0 + fVar23 * local_1d4 + fVar6 * local_1f8;
      fVar17 = fVar24 * local_1e4;
      fVar18 = fVar23 * local_1e4;
      local_1e4 = fVar22 * local_1f0 + fVar2 * local_1e4 + fVar3 * local_res10;
      local_1f0 = fVar4 * local_1f0 + fVar17 + fVar25 * local_res10;
      local_res10 = fVar21 + fVar18 + fVar6 * local_res10;
      fVar17 = fVar22 * local_1ec + fVar2 * local_1e8 + fVar3 * local_1f4;
      fVar22 = fVar4 * local_1ec + fVar24 * local_1e8 + fVar25 * local_1f4;
      local_1f4 = fVar5 * local_1ec + fVar23 * local_1e8 + fVar6 * local_1f4;
      fVar23 = fVar23 * fVar19;
      fVar24 = fVar24 * fVar19;
      fVar25 = fVar25 * local_1a4;
      fVar19 = pfVar13[1] * fVar27 + fVar2 * fVar19 + fVar3 * local_1a4 + pfVar13[3];
      local_1a4 = fVar5 * fVar27 + fVar23 + fVar6 * local_1a4 + pfVar13[0xb];
      fVar27 = fVar4 * fVar27 + fVar24 + fVar25 + pfVar13[7];
      local_19c = fVar20;
    }
    param_1[5] = local_1f0;
    param_1[6] = fVar22;
    param_1[8] = local_1f8;
    param_1[10] = local_1f4;
    *param_1 = fVar20;
    param_1[1] = local_1e4;
    param_1[2] = fVar17;
    param_1[3] = fVar19;
    param_1[4] = fVar26;
    param_1[0xb] = local_1a4;
  }
  else {
    cVar11 = (**(code **)(*param_2 + 0x280))(param_2,param_3,local_138);
    if (cVar11 == '\0') goto LAB_1811a668e;
    if (local_108 != (longlong *)0x0) {
      pfVar13 = (float *)(**(code **)(*local_108 + 0x160))(local_108,param_4);
      fVar27 = pfVar13[2];
      fVar22 = pfVar13[3];
      fVar19 = pfVar13[4];
      fVar17 = pfVar13[5];
      fVar26 = *pfVar13;
      fVar20 = pfVar13[1];
      fVar2 = pfVar13[8];
      fVar3 = pfVar13[9];
      fVar24 = pfVar13[10];
      fVar4 = pfVar13[6];
      fVar5 = pfVar13[7];
      local_19c = pfVar13[0xb];
      pfVar13 = (float *)(**(code **)(*param_2 + 0x290))(param_2,param_3,0);
      fVar6 = *pfVar13;
      fVar25 = pfVar13[1];
      fVar23 = pfVar13[2];
      fVar18 = pfVar13[4];
      fVar21 = pfVar13[5];
      fVar7 = pfVar13[6];
      fVar8 = pfVar13[8];
      local_1d4 = fVar19 * fVar25 + fVar6 * fVar26 + fVar2 * fVar23;
      local_1a0 = fVar19 * fVar21 + fVar18 * fVar26 + fVar2 * fVar7;
      fVar9 = pfVar13[9];
      fVar10 = pfVar13[10];
      local_1f8 = fVar19 * fVar9 + fVar8 * fVar26 + fVar2 * fVar10;
      local_1e4 = fVar17 * fVar25 + fVar20 * fVar6 + fVar3 * fVar23;
      local_1f0 = fVar17 * fVar21 + fVar20 * fVar18 + fVar3 * fVar7;
      local_res10 = fVar17 * fVar9 + fVar8 * fVar20 + fVar3 * fVar10;
      local_1e8 = fVar27 * fVar6 + fVar4 * fVar25 + fVar24 * fVar23;
      local_1ec = fVar4 * fVar21 + fVar27 * fVar18 + fVar24 * fVar7;
      local_1f4 = fVar4 * fVar9 + fVar27 * fVar8 + fVar24 * fVar10;
      local_1c0 = local_1a0;
      fVar19 = fVar22 * fVar6 + fVar5 * fVar25 + local_19c * fVar23 + pfVar13[3];
      fVar27 = fVar5 * fVar21 + fVar22 * fVar18 + local_19c * fVar7 + pfVar13[7];
      local_1a4 = fVar5 * fVar9 + fVar22 * fVar8 + local_19c * fVar10 + pfVar13[0xb];
      goto LAB_1811a6733;
    }
    if (local_100 == (longlong *)0x0) {
LAB_1811a668e:
      local_1f4 = local_1a8;
      goto LAB_1811a66c7;
    }
    plVar14 = (longlong *)(**(code **)(*local_100 + 0x48))(local_100);
    plVar14 = (longlong *)(**(code **)(*plVar14 + 0x38))(plVar14,param_4);
    if (plVar14 == (longlong *)0x0) {
      plVar14 = (longlong *)(**(code **)(*local_100 + 0x58))(local_100);
      plVar16 = (longlong *)(**(code **)(*local_100 + 0x38))(local_100);
      iVar12 = (**(code **)(*plVar14 + 0x38))(plVar14,param_4);
      if (-1 < iVar12) {
        if (param_6 == '\0') {
          pfVar13 = (float *)(**(code **)(*plVar16 + 0xc0))(plVar16,iVar12);
        }
        else {
          pfVar13 = (float *)(**(code **)(*plVar16 + 200))();
        }
        local_198 = *pfVar13;
        local_194 = pfVar13[1];
        local_190 = pfVar13[2];
        local_18c = pfVar13[3];
        local_188 = pfVar13[4];
        fVar22 = local_194 + local_194;
        local_180 = pfVar13[6];
        fVar27 = pfVar13[5];
        fVar19 = local_190 + local_190;
        fVar26 = 1.0 - local_198 * (local_198 + local_198);
        fVar17 = local_18c * (local_198 + local_198);
        param_1[2] = local_198 * fVar19 + local_18c * fVar22;
        param_1[3] = local_188;
        param_1[8] = local_198 * fVar19 - local_18c * fVar22;
        param_1[1] = local_198 * fVar22 - local_18c * fVar19;
        param_1[4] = local_18c * fVar19 + local_198 * fVar22;
        param_1[10] = fVar26 - local_194 * fVar22;
        param_1[6] = local_194 * fVar19 - fVar17;
        *param_1 = (1.0 - local_194 * fVar22) - local_190 * fVar19;
        param_1[5] = fVar26 - local_190 * fVar19;
        param_1[9] = local_194 * fVar19 + fVar17;
        param_1[0xb] = local_180;
        local_184 = fVar27;
        goto LAB_1811a69d0;
      }
      if (param_6 == '\0') {
        pfVar13 = (float *)(**(code **)(*param_2 + 0x290))(param_2,param_3,0);
        fVar27 = pfVar13[1];
        fVar22 = *pfVar13;
        fVar19 = pfVar13[2];
        fVar17 = pfVar13[4];
        fVar26 = pfVar13[5];
        fVar20 = pfVar13[6];
        fVar2 = pfVar13[9];
        fVar3 = pfVar13[10];
        local_1d4 = fVar27 * local_1c0 + fVar22 * local_1d0 + fVar19 * local_1b0;
        local_1a0 = fVar26 * local_1c0 + fVar17 * local_1d0 + fVar20 * local_1b0;
        fVar24 = pfVar13[8];
        local_1f8 = fVar2 * local_1c0 + fVar24 * local_1d0 + fVar3 * local_1b0;
        local_1e4 = fVar27 * local_1bc + fVar22 * local_1cc + fVar19 * local_1ac;
        local_1f0 = fVar26 * local_1bc + fVar17 * local_1cc + fVar20 * local_1ac;
        local_res10 = fVar2 * local_1bc + fVar24 * local_1cc + fVar3 * local_1ac;
        local_1e8 = fVar27 * local_1b8 + fVar22 * local_1c8 + fVar19 * local_1a8;
        local_1ec = fVar26 * local_1b8 + fVar17 * local_1c8 + fVar20 * local_1a8;
        local_1f4 = fVar2 * local_1b8 + fVar24 * local_1c8 + fVar3 * local_1a8;
        local_1c0 = local_1a0;
        fVar19 = fVar27 * local_1b4 + fVar22 * local_1c4 + fVar19 * local_1a4 + pfVar13[3];
        fVar27 = fVar26 * local_1b4 + fVar17 * local_1c4 + fVar20 * local_1a4 + pfVar13[7];
        local_1a4 = fVar2 * local_1b4 + fVar24 * local_1c4 + fVar3 * local_1a4 + pfVar13[0xb];
        goto LAB_1811a6733;
      }
      goto LAB_1811a668e;
    }
    if (param_5 == '\0') {
      puVar15 = (undefined8 *)(**(code **)(*plVar14 + 0x68))(plVar14);
      local_15c = 0x3f800000;
      local_178 = *puVar15;
      uStack_170 = puVar15[1];
      local_160 = *(undefined4 *)(puVar15 + 3);
      pfVar13 = (float *)&local_178;
      local_168 = puVar15[2];
    }
    else {
      pfVar13 = (float *)(**(code **)(*plVar14 + 0x70))(plVar14,&local_1d0);
    }
    local_13c = pfVar13[7];
    local_158 = *pfVar13;
    local_154 = pfVar13[1];
    local_150 = pfVar13[2];
    local_14c = pfVar13[3];
    fVar22 = local_154 + local_154;
    local_148 = pfVar13[4];
    local_140 = pfVar13[6];
    fVar27 = pfVar13[5];
    fVar17 = local_14c * (local_158 + local_158);
    fVar19 = local_150 + local_150;
    fVar26 = 1.0 - local_158 * (local_158 + local_158);
    param_1[2] = (local_158 * fVar19 + local_14c * fVar22) * local_13c;
    *param_1 = ((1.0 - local_154 * fVar22) - local_150 * fVar19) * local_13c;
    param_1[3] = local_148;
    param_1[6] = (local_154 * fVar19 - fVar17) * local_13c;
    param_1[8] = (local_158 * fVar19 - local_14c * fVar22) * local_13c;
    param_1[0xb] = local_140;
    param_1[1] = (local_158 * fVar22 - local_14c * fVar19) * local_13c;
    local_res10 = (local_154 * fVar19 + fVar17) * local_13c;
    param_1[4] = (local_14c * fVar19 + local_158 * fVar22) * local_13c;
    param_1[5] = (fVar26 - local_150 * fVar19) * local_13c;
    param_1[10] = (fVar26 - local_154 * fVar22) * local_13c;
    local_144 = fVar27;
  }
  param_1[9] = local_res10;
LAB_1811a69d0:
  param_1[7] = fVar27;
  if ((local_130 != 0) && (local_128 < 0)) {
    LOCK();
    piVar1 = (int *)(local_130 + -4);
    iVar12 = *piVar1;
    *piVar1 = *piVar1 + -1;
    UNLOCK();
    if (iVar12 == 1) {
      free((void *)(local_130 + -4));
    }
  }
  return param_1;
}


