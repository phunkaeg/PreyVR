
void FUN_1816a29e0(longlong param_1)

{
  longlong *plVar1;
  float fVar2;
  float fVar3;
  bool bVar4;
  char cVar5;
  undefined4 uVar6;
  undefined4 uVar7;
  undefined4 uVar8;
  int iVar9;
  longlong *plVar10;
  longlong lVar11;
  undefined8 uVar12;
  undefined8 uVar13;
  float *pfVar14;
  longlong *plVar15;
  longlong *plVar16;
  float fVar17;
  float fVar18;
  float fVar19;
  float fVar20;
  float fVar21;
  undefined1 auVar22 [16];
  float fVar23;
  float fVar24;
  float fVar25;
  float fVar26;
  float fVar27;
  float local_res10 [2];
  float local_res18;
  longlong local_res20;
  undefined8 in_stack_fffffffffffffd88;
  undefined8 uVar28;
  ulonglong in_stack_fffffffffffffd90;
  float local_248;
  float local_244;
  float local_240;
  float local_238;
  float local_234;
  float local_230;
  undefined8 local_228;
  float local_220;
  float local_21c;
  float local_218;
  float local_210;
  float local_20c;
  float local_208;
  float local_200;
  float local_1fc;
  float local_1f8;
  longlong *local_1f0;
  undefined1 local_1e8 [8];
  undefined4 local_1e0;
  longlong alStack_1c8 [2];
  int aiStack_1b8 [3];
  float local_1ac;
  float local_1a8;
  float local_1a4;
  float local_1a0;
  float local_19c;
  float local_198;
  undefined4 auStack_174 [3];
  undefined2 local_168;
  undefined2 local_166;
  undefined8 local_158;
  undefined1 local_150;
  undefined4 local_14c;
  undefined1 local_148 [4];
  float local_144;
  float local_13c;
  float local_134;
  float local_12c;
  float local_124;
  float local_11c;
  undefined1 local_118 [16];
  int local_108;
  longlong *local_100 [24];
  
  if (*(char *)(param_1 + 0x2fd) == '\0') {
    plVar1 = (longlong *)(param_1 + -400);
    cVar5 = FUN_181697ab0(plVar1);
    if (cVar5 != '\0') {
      fVar17 = (float)(**(code **)(*DAT_18224da18 + 0x38))(DAT_18224da18,3);
      plVar10 = DAT_18224da38;
      lVar11 = *DAT_18224da38;
      uVar6 = (**(code **)(*(longlong *)(param_1 + -0x188) + 0x1d8))(param_1 + -0x188);
      plVar10 = (longlong *)(**(code **)(lVar11 + 0x70))(plVar10,uVar6);
      local_1f0 = plVar10;
      if ((*(char *)(param_1 + 0x2fc) == '\0') &&
         (cVar5 = (**(code **)(*plVar1 + 0x208))(plVar1), cVar5 != '\0')) {
        fVar23 = *(float *)(param_1 + 0x3ec);
      }
      else {
        fVar23 = 0.0;
      }
      if ((*(longlong *)(param_1 + 0x380) != 0) && (fVar23 != *(float *)(param_1 + 0x3f0))) {
        if (0.0 < *(float *)(param_1 + 0x370)) {
          fVar18 = *(float *)(param_1 + 0x3f0);
          fVar25 = (*(float *)(param_1 + 0x378) / *(float *)(param_1 + 0x370)) * fVar17;
          if (fVar18 <= fVar23) {
            fVar18 = fVar18 + fVar25;
            if (fVar23 <= fVar18) {
              fVar18 = fVar23;
            }
          }
          else {
            fVar18 = fVar18 - fVar25;
            if (fVar18 <= fVar23) {
              fVar18 = fVar23;
            }
          }
          *(float *)(param_1 + 0x3f0) = fVar18;
        }
        else {
          *(float *)(param_1 + 0x3f0) = fVar23;
        }
        local_res10[0] = *(float *)(param_1 + 0x3f0);
        (**(code **)(**(longlong **)(param_1 + 0x380) + 0x100))
                  (*(longlong **)(param_1 + 0x380),4,local_res10);
      }
      if (*(char *)(param_1 + 0x139) != '\0') {
        uVar13 = *(undefined8 *)(param_1 + -0x150);
        in_stack_fffffffffffffd90 = in_stack_fffffffffffffd90 & 0xffffffffffffff00;
        uVar28 = CONCAT71((int7)((ulonglong)in_stack_fffffffffffffd88 >> 8),1);
        Prey_GetEntitySlotHelperWorldTM
                  (local_148,uVar13,0,*(undefined8 *)(param_1 + 0x160),uVar28,
                   in_stack_fffffffffffffd90);
        uVar6 = (undefined4)(in_stack_fffffffffffffd90 >> 0x20);
        local_238 = local_13c;
        local_230 = local_11c;
        local_234 = local_12c;
        fVar23 = local_144 * local_144 + local_134 * local_134 + local_124 * local_124 +
                 1.1754944e-38;
        auVar22 = rsqrtss(ZEXT416((uint)fVar23),ZEXT416((uint)fVar23));
        local_res10[0] = auVar22._0_4_;
        local_240 = (1.5 - local_res10[0] * fVar23 * local_res10[0] * 0.5) * local_res10[0];
        local_248 = local_144 * local_240;
        local_244 = local_134 * local_240;
        local_240 = local_124 * local_240;
        lVar11 = TryGetLocalArkPlayer();
        if (lVar11 != 0) {
          uVar12 = FUN_18119a470(lVar11 + 0x678);
          cVar5 = FUN_181272950(uVar12);
          if (cVar5 != '\0') {
            GetReticleInfoForFiring_ArkWeapon(plVar1,&local_228);
            local_248 = local_220 - local_238;
            local_21c = local_21c - local_234;
            local_218 = local_218 - local_230;
            fVar23 = local_21c * local_21c + local_248 * local_248 + local_218 * local_218;
            auVar22 = rsqrtss(ZEXT416((uint)fVar23),ZEXT416((uint)fVar23));
            local_res10[0] = auVar22._0_4_;
            local_240 = (1.5 - local_res10[0] * fVar23 * local_res10[0] * 0.5) * local_res10[0];
            local_248 = local_248 * local_240;
            local_244 = local_240 * local_21c;
            local_240 = local_218 * local_240;
          }
        }
        uVar12 = 0;
        local_166 = 0xffff;
        local_168 = 0xffff;
        local_1fc = local_234 - local_244 * 0.7;
        local_200 = local_238 - local_248 * 0.7;
        local_1e0 = 2;
        local_14c = 0x447a0000;
        local_1f8 = local_230 - local_240 * 0.7;
        local_158 = 0;
        local_150 = 0;
        alStack_1c8[1] = 0;
        fVar23 = (float)(**(code **)(*plVar1 + 0x140))(plVar1);
        uVar7 = (**(code **)(*(longlong *)(param_1 + -0x188) + 0x1d8))(param_1 + -0x188);
        pfVar14 = (float *)CONCAT71((int7)((ulonglong)uVar28 >> 8),1);
        fVar18 = (float)FUN_1816ad790(local_1e8,&local_200,&local_248,fVar23 + 0.7,pfVar14,
                                      CONCAT44(uVar6,uVar7),uVar13,1,1,0);
        if (*(float *)(param_1 + 0x368) <= 0.0) {
          fVar25 = 0.0;
        }
        else {
          fVar25 = *(float *)(param_1 + 1000) / *(float *)(param_1 + 0x368);
          if (1.0 <= fVar25) {
            fVar25 = 1.0;
          }
        }
        *(float *)(param_1 + 0x3bc) = fVar25;
        if (plVar10 != (longlong *)0x0) {
          uVar12 = (**(code **)(*plVar10 + 0x228))(plVar10);
        }
        FUN_1811a4c90(local_100,local_1e8,uVar12);
        fVar20 = local_240;
        fVar27 = local_244;
        fVar25 = local_248;
        local_res10[0] = (float)CONCAT31(local_res10[0]._1_3_,0.0 < fVar18);
        local_res18 = local_238 + local_248 * fVar23;
        fVar26 = local_244 * fVar23 + local_234;
        fVar23 = local_240 * fVar23 + local_230;
        if (0.0 < fVar18) {
          local_res20 = param_1 + 0x390;
          FUN_1816715e0(local_res20,
                        SQRT((local_1ac - local_238) * (local_1ac - local_238) +
                             (local_1a8 - local_234) * (local_1a8 - local_234) +
                             (local_1a4 - local_230) * (local_1a4 - local_230)));
          uVar7 = (undefined4)((ulonglong)pfVar14 >> 0x20);
          uVar6 = (undefined4)((ulonglong)uVar13 >> 0x20);
          plVar10 = *(longlong **)(param_1 + 0x3c8);
          bVar4 = false;
          for (plVar15 = *(longlong **)(param_1 + 0x3c0); plVar15 != plVar10; plVar15 = plVar15 + 1)
          {
            plVar16 = (longlong *)*plVar15;
            if (plVar16 != (longlong *)0x0) {
              (**(code **)(*plVar16 + 8))(plVar16);
            }
            if (bVar4) {
              FUN_181671600(plVar16);
            }
            else {
              pfVar14 = &local_238;
              cVar5 = FUN_1816a3600(plVar1);
              if (cVar5 == '\0') {
                bVar4 = true;
              }
            }
            if (plVar16 != (longlong *)0x0) {
              (**(code **)(*plVar16 + 0x10))(plVar16);
            }
            uVar7 = (undefined4)((ulonglong)pfVar14 >> 0x20);
            uVar6 = (undefined4)((ulonglong)uVar13 >> 0x20);
          }
          FUN_1811a4c90(local_118,local_1e8,uVar12);
          if (aiStack_1b8[local_108] == 2) {
            plVar16 = (longlong *)alStack_1c8[local_108];
            if (plVar16 != (longlong *)0x0) {
              uVar8 = (**(code **)(*plVar16 + 8))(plVar16);
              FUN_1816a04b0(plVar1);
              local_210 = local_1a0;
              local_20c = local_19c;
              local_208 = local_198;
              local_220 = local_1a4;
              uVar13 = (**(code **)(*plVar16 + 0x228))(plVar16);
              FUN_1816ae1c0(plVar16,uVar13,&local_228,&local_210,
                            CONCAT44(uVar7,auStack_174[local_108]),plVar1,CONCAT44(uVar6,0x3f800000)
                            ,0,0x7f7fffff);
              uVar13 = *(undefined8 *)(DAT_182c16840 + 0x2f8);
              cVar5 = FUN_181486bb0(uVar13,*(undefined4 *)(param_1 + 0x450));
              if (cVar5 == '\0') {
                uVar6 = *(undefined4 *)(param_1 + -0x158);
                uVar7 = (**(code **)(*(longlong *)(param_1 + -0x188) + 0x1d8))();
                uVar7 = FUN_18141d5f0(uVar13,uVar6,uVar7,0x3f800000);
                *(undefined4 *)(param_1 + 0x450) = uVar7;
                FUN_18141e020(uVar13,uVar8,uVar6);
              }
              else {
                FUN_18141e120(uVar13,*(undefined4 *)(param_1 + 0x450),uVar8);
              }
            }
          }
          else {
            plVar16 = (longlong *)0x0;
          }
          fVar23 = local_19c * local_19c + local_1a0 * local_1a0 + local_198 * local_198;
          auVar22 = rsqrtss(ZEXT416((uint)fVar23),ZEXT416((uint)fVar23));
          fVar20 = auVar22._0_4_;
          fVar20 = (1.5 - fVar20 * fVar23 * fVar20 * 0.5) * fVar20;
          fVar25 = local_1a0 * fVar20;
          fVar27 = local_19c * fVar20;
          fVar20 = local_198 * fVar20;
          lVar11 = local_res20;
          fVar18 = local_1ac;
          fVar26 = local_1a8;
          fVar23 = local_1a4;
        }
        else {
          lVar11 = param_1 + 0x390;
          local_res20 = lVar11;
          FUN_1816715e0(lVar11);
          plVar10 = *(longlong **)(param_1 + 0x3c8);
          fVar18 = local_res18;
          for (plVar15 = *(longlong **)(param_1 + 0x3c0); plVar16 = local_100[0],
              local_res18 = fVar18, plVar15 != plVar10; plVar15 = plVar15 + 1) {
            plVar16 = (longlong *)*plVar15;
            if (plVar16 != (longlong *)0x0) {
              (**(code **)(*plVar16 + 8))(plVar16);
            }
            FUN_181671600(plVar16);
            if (plVar16 != (longlong *)0x0) {
              (**(code **)(*plVar16 + 0x10))(plVar16);
            }
            lVar11 = local_res20;
            fVar18 = local_res18;
          }
        }
        if (local_1f0 == (longlong *)0x0) {
          local_228 = 0;
          local_220 = 1.0;
          FUN_181671690(lVar11,&local_238,&local_248,&local_228);
        }
        else {
          pfVar14 = (float *)(**(code **)(*local_1f0 + 0x150))(local_1f0,&local_210);
          fVar2 = pfVar14[3];
          fVar3 = pfVar14[2];
          fVar19 = pfVar14[1] * fVar2;
          fVar21 = *pfVar14 * fVar3;
          fVar24 = pfVar14[1] * fVar3 - *pfVar14 * fVar2;
          local_228 = CONCAT44(fVar24 + fVar24,fVar21 + fVar21 + fVar19 + fVar19);
          local_220 = ((fVar2 + fVar2) * fVar2 + (fVar3 + fVar3) * fVar3) - 1.0;
          FUN_181671690(lVar11,&local_238,&local_248,&local_228);
        }
        local_228 = CONCAT44(fVar27,fVar25);
        local_220 = fVar20;
        local_210 = fVar18;
        local_20c = fVar26;
        local_208 = fVar23;
        FUN_1816a3e60(plVar1,local_res10[0]._0_1_,&local_210,&local_228);
        fVar23 = (float)FUN_181695020(plVar1,&DAT_182bd6f98);
        fVar18 = (float)FUN_181695020(plVar1,&DAT_182bd6f90);
        if (*(int *)(param_1 + 0x350) == 0) {
          fVar25 = (float)FUN_181695020(plVar1,&DAT_182bd6fa8);
          if ((0.0 < fVar25) && (*(float *)(param_1 + 1000) < fVar25)) {
            fVar18 = (fVar18 - fVar23) * (*(float *)(param_1 + 1000) / fVar25) + fVar23;
          }
          *(float *)(param_1 + 0x3e0) = fVar18;
        }
        else if (*(int *)(param_1 + 0x350) == 1) {
          fVar23 = (float)FUN_181695020(plVar1,&DAT_182bd6f78);
          fVar23 = (fVar23 + 1.0) * *(float *)(param_1 + 0x3e0);
          if (fVar18 <= fVar23) {
            fVar23 = fVar18;
          }
          *(float *)(param_1 + 0x3e0) = fVar23;
        }
        if (plVar16 == (longlong *)0x0) {
          iVar9 = 0;
        }
        else {
          iVar9 = (**(code **)(*plVar16 + 8))(plVar16);
        }
        if (((*(int *)(param_1 + 0x3e4) == 0) || (plVar16 == (longlong *)0x0)) ||
           (*(int *)(param_1 + 0x3e4) != iVar9)) {
          if (*(int *)(param_1 + 0x354) == 1) {
            *(undefined4 *)(param_1 + 1000) = 0;
            uVar6 = FUN_181695020(plVar1,&DAT_182bd6f98);
            *(undefined4 *)(param_1 + 0x3e0) = uVar6;
          }
          *(int *)(param_1 + 0x3e4) = iVar9;
          *(undefined4 *)(param_1 + 0x3dc) = 0;
        }
        else if (*(int *)(param_1 + 0x354) == 1) {
          *(float *)(param_1 + 1000) = fVar17 + *(float *)(param_1 + 1000);
        }
        if (*(int *)(param_1 + 0x354) == 0) {
          *(float *)(param_1 + 1000) = fVar17 + *(float *)(param_1 + 1000);
        }
      }
    }
  }
  return;
}


