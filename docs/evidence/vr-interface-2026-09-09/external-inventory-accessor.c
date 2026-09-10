
void FUN_18161d800(longlong *param_1,undefined1 param_2)

{
  int *piVar1;
  undefined4 uVar2;
  longlong *plVar3;
  longlong lVar4;
  longlong **pplVar5;
  char cVar6;
  char cVar7;
  int iVar8;
  undefined4 uVar9;
  longlong lVar10;
  undefined8 uVar11;
  undefined4 *puVar12;
  undefined8 uVar13;
  longlong *plVar14;
  longlong lVar15;
  ulonglong uVar16;
  size_t sVar17;
  char *pcVar18;
  longlong **pplVar19;
  size_t sVar20;
  char *pcVar21;
  size_t sVar22;
  undefined1 uVar23;
  int iVar24;
  char *pcVar25;
  longlong *local_res18;
  char *local_res20;
  char *local_88;
  size_t local_80;
  longlong *local_78;
  char *local_70;
  undefined8 local_68;
  longlong local_60;
  longlong **local_58;
  longlong **local_50;
  size_t *local_48;
  
  local_60 = (**(code **)(*DAT_18224dad0 + 0x60))(DAT_18224dad0,"DanielleInventoryExternal");
  if (local_60 == 0) {
    return;
  }
  local_80 = param_1[2];
  lVar10 = GetArkPlayerInstance();
  uVar11 = FUN_181587c40(lVar10 + 0x678);
  sVar22 = 0xffffffffffffffff;
  sVar17 = 0xffffffffffffffff;
  uVar2 = *(undefined4 *)(local_80 + 0x28);
  local_68 = uVar11;
  if (*(char *)(local_80 + 0x34) != '\0') {
    local_res18 = (longlong *)&DAT_18224d03c;
    do {
      sVar20 = sVar17;
      sVar17 = sVar20 + 1;
    } while ("@ui_Cancel"[sVar20 + 1] != '\0');
    if (sVar17 != 0) {
      puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
      *puVar12 = 1;
      local_res18 = (longlong *)(puVar12 + 3);
      puVar12[1] = (int)sVar17;
      puVar12[2] = (int)sVar17;
      *(char *)(sVar17 + (longlong)local_res18) = '\0';
      if (local_res18 != (longlong *)"@ui_Cancel") {
        memcpy(local_res18,"@ui_Cancel",sVar17);
      }
    }
    FUN_18163c5f0(uVar11,1,1,DAT_182c16838 + 0x670,&local_res18,0,1);
    iVar8 = *(int *)((longlong)local_res18 + -0xc);
    if ((-1 < iVar8) &&
       (iVar8 = iVar8 + -1, *(int *)((longlong)local_res18 + -0xc) = iVar8, iVar8 < 1)) {
      FUN_1800a8b20();
    }
    lVar10 = *param_1;
    uVar9 = FUN_18162c130(param_1[2]);
    cVar6 = (**(code **)(lVar10 + 0x60))(param_1,uVar2,uVar9);
    local_res18 = (longlong *)&DAT_18224d03c;
    if (cVar6 == '\0') {
      pcVar18 = "@ui_Place";
      do {
        sVar17 = sVar22 + 1;
        lVar10 = sVar22 + 1;
        sVar22 = sVar17;
      } while ("@ui_Place"[lVar10] != '\0');
    }
    else {
      pcVar18 = "@ui_Stack";
      do {
        sVar17 = sVar22 + 1;
        lVar10 = sVar22 + 1;
        sVar22 = sVar17;
      } while ("@ui_Stack"[lVar10] != '\0');
    }
    if (sVar17 != 0) {
      puVar12 = (undefined4 *)FUN_1800a8b30(sVar17 + 0xd,0xf);
      *puVar12 = 1;
      local_res18 = (longlong *)(puVar12 + 3);
      puVar12[1] = (int)sVar17;
      puVar12[2] = (int)sVar17;
      *(char *)(sVar17 + (longlong)local_res18) = '\0';
      if (local_res18 != (longlong *)pcVar18) {
        memcpy(local_res18,pcVar18,sVar17);
      }
    }
    FUN_18163c5f0(uVar11,2,1,DAT_182c16838 + 0x668,&local_res18,0,1);
    iVar8 = *(int *)((longlong)local_res18 + -0xc);
    if ((-1 < iVar8) &&
       (iVar8 = iVar8 + -1, *(int *)((longlong)local_res18 + -0xc) = iVar8, iVar8 < 1)) {
      FUN_1800a8b20();
    }
    FUN_181636db0(uVar11,3);
    goto LAB_18161e791;
  }
  local_res20 = &DAT_18224d03c;
  if (*(int *)(*(longlong *)(DAT_182c16840 + 0x1a8) + 0x28) == 2) {
    do {
      sVar20 = sVar17;
      sVar17 = sVar20 + 1;
    } while ("@ui_Back"[sVar20 + 1] != '\0');
    if (sVar17 != 0) {
      puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
      *puVar12 = 1;
      local_res20 = (char *)(puVar12 + 3);
      puVar12[1] = (int)sVar17;
      puVar12[2] = (int)sVar17;
      local_res20[sVar17] = '\0';
      if (local_res20 != "@ui_Back") {
        memcpy(local_res20,"@ui_Back",sVar17);
      }
    }
    lVar10 = DAT_182c16838 + 0x680;
  }
  else {
    do {
      sVar20 = sVar17;
      sVar17 = sVar20 + 1;
    } while ("@ui_Back"[sVar20 + 1] != '\0');
    if (sVar17 != 0) {
      puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
      *puVar12 = 1;
      local_res20 = (char *)(puVar12 + 3);
      puVar12[1] = (int)sVar17;
      puVar12[2] = (int)sVar17;
      local_res20[sVar17] = '\0';
      if (local_res20 != "@ui_Back") {
        memcpy(local_res20,"@ui_Back",sVar17);
      }
    }
    lVar10 = DAT_182c16838 + 0x670;
  }
  FUN_18163c5f0(uVar11,1,1,lVar10,&local_res20,0,1);
  iVar8 = *(int *)(local_res20 + -0xc);
  if ((-1 < iVar8) && (iVar8 = iVar8 + -1, *(int *)(local_res20 + -0xc) = iVar8, iVar8 < 1)) {
    FUN_1800a8b20();
  }
  lVar10 = FUN_18162bff0(local_80);
  if (lVar10 == 0) {
    lVar10 = 0;
  }
  else {
    lVar10 = lVar10 + -0x50;
  }
  uVar13 = FUN_1810d8940(lVar10,&local_70);
  FUN_18163c5f0(uVar11,2,1,DAT_182c16838 + 0x6e0,uVar13,0,1);
  iVar24 = 3;
  local_res20 = (char *)CONCAT44(local_res20._4_4_,3);
  iVar8 = *(int *)(local_70 + -0xc);
  if ((-1 < iVar8) && (iVar8 = iVar8 + -1, *(int *)(local_70 + -0xc) = iVar8, iVar8 < 1)) {
    FUN_1800a8b20();
  }
  local_78 = (longlong *)FUN_18117f3e0(uVar2);
  if ((local_78 != (longlong *)0x0) &&
     (cVar6 = (**(code **)(*local_78 + 0x140))(local_78), cVar6 != '\0')) {
    local_70 = &DAT_18224d03c;
    sVar17 = 0xffffffffffffffff;
    do {
      sVar20 = sVar17;
      sVar17 = sVar20 + 1;
    } while ("@ui_Split"[sVar20 + 1] != '\0');
    if (sVar17 != 0) {
      puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
      *puVar12 = 1;
      local_70 = (char *)(puVar12 + 3);
      puVar12[1] = (int)sVar17;
      puVar12[2] = (int)sVar17;
      local_70[sVar17] = '\0';
      if (local_70 != "@ui_Split") {
        memcpy(local_70,"@ui_Split",sVar17);
      }
    }
    iVar8 = (**(code **)(*local_78 + 0xf0))();
    FUN_18163c5f0(uVar11,3,1,DAT_182c16838 + 0x700,&local_70,0,1 < iVar8);
    iVar24 = 4;
    local_res20 = (char *)CONCAT44(local_res20._4_4_,4);
    iVar8 = *(int *)(local_70 + -0xc);
    if ((-1 < iVar8) && (iVar8 = iVar8 + -1, *(int *)(local_70 + -0xc) = iVar8, iVar8 < 1)) {
      FUN_1800a8b20();
    }
  }
  sVar17 = local_80;
  iVar8 = (**(code **)**(undefined8 **)(local_80 + 0x40))();
  local_58 = &local_78;
  local_50 = &local_res18;
  local_res18 = (longlong *)CONCAT71(local_res18._1_7_,iVar8 < *(int *)(sVar17 + 0x20));
  local_48 = &local_80;
  FUN_18161b710(&local_58,&local_70);
  pcVar18 = local_70;
  if (local_78 != (longlong *)0x0) {
    if (*(int *)(local_70 + -8) == 0) {
      local_70 = &DAT_18224d03c;
      sVar17 = 0xffffffffffffffff;
      do {
        sVar20 = sVar17;
        sVar17 = sVar20 + 1;
      } while ("@ui_Move"[sVar20 + 1] != '\0');
      if (sVar17 != 0) {
        puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
        *puVar12 = 1;
        local_70 = (char *)(puVar12 + 3);
        puVar12[1] = (int)sVar17;
        puVar12[2] = (int)sVar17;
        local_70[sVar17] = '\0';
        if (local_70 != "@ui_Move") {
          memcpy(local_70,"@ui_Move",sVar17);
        }
      }
      FUN_18163c5f0(uVar11,iVar24,1,DAT_182c16838 + 0x720,&local_70,0,1);
      iVar24 = iVar24 + 1;
      local_res20 = (char *)CONCAT44(local_res20._4_4_,iVar24);
      iVar8 = *(int *)(local_70 + -0xc);
      if ((-1 < iVar8) && (iVar8 = iVar8 + -1, *(int *)(local_70 + -0xc) = iVar8, iVar8 < 1)) {
        FUN_1800a8b20();
      }
    }
    cVar6 = (**(code **)(*local_78 + 0x1a8))();
    if (cVar6 != '\0') {
      local_70 = (char *)local_78[0x26];
      if (*(int *)(local_70 + -0xc) < 0) {
        local_70 = &DAT_18224d03c;
      }
      else {
        *(int *)(local_70 + -0xc) = *(int *)(local_70 + -0xc) + 1;
      }
      FUN_18163c5f0(uVar11,iVar24,1,DAT_182c16838 + 0x770,&local_70,0,1);
      local_res20 = (char *)CONCAT44(local_res20._4_4_,iVar24 + 1);
      iVar8 = *(int *)(local_70 + -0xc);
      if ((-1 < iVar8) && (iVar8 = iVar8 + -1, *(int *)(local_70 + -0xc) = iVar8, iVar8 < 1)) {
        FUN_1800a8b20();
      }
    }
  }
  plVar3 = *(longlong **)(local_80 + 0x48);
  cVar6 = (**(code **)(*plVar3 + 0x98))(plVar3);
  plVar14 = plVar3;
  if ((char)local_res18 == '\0') {
    plVar14 = *(longlong **)(local_80 + 0x40);
  }
  cVar7 = (**(code **)(*plVar14 + 0xa0))(plVar14);
  if (cVar7 == '\0') {
    if ((char)local_res18 == '\0') {
      if (cVar6 == '\0') goto LAB_18161e1c8;
      (**(code **)(**(longlong **)(local_80 + 0x40) + 0x48))
                (*(longlong **)(local_80 + 0x40),&local_58);
      pplVar5 = local_50;
      for (pplVar19 = local_58; pplVar19 != pplVar5;
          pplVar19 = (longlong **)((longlong)pplVar19 + 4)) {
        plVar14 = (longlong *)FUN_18117f3e0(*(undefined4 *)pplVar19);
        cVar6 = (**(code **)(*plVar14 + 0x160))(plVar14);
        if ((cVar6 != '\0') && (cVar6 = FUN_1810dce00(plVar14,plVar3), cVar6 != '\0')) {
          if (local_58 != (longlong **)0x0) {
            uVar16 = (longlong)local_48 - (longlong)local_58 >> 2;
            if ((0x3fffffffffffffff < uVar16) ||
               ((pplVar19 = local_58, 0xfff < uVar16 * 4 &&
                (((((ulonglong)local_58 & 0x1f) != 0 ||
                  (pplVar19 = (longlong **)local_58[-1], local_58 <= pplVar19)) ||
                 (0x1f < (ulonglong)((longlong)local_58 + (-8 - (longlong)pplVar19)))))))) {
                    /* WARNING: Subroutine does not return */
              _invalid_parameter_noinfo_noreturn();
            }
            free(pplVar19);
          }
          uVar23 = 1;
          goto LAB_18161e142;
        }
      }
      if (local_58 != (longlong **)0x0) {
        uVar16 = (longlong)local_48 - (longlong)local_58 >> 2;
        if ((0x3fffffffffffffff < uVar16) ||
           ((pplVar19 = local_58, 0xfff < uVar16 * 4 &&
            (((((ulonglong)local_58 & 0x1f) != 0 ||
              (pplVar19 = (longlong **)local_58[-1], local_58 <= pplVar19)) ||
             (0x1f < (ulonglong)((longlong)local_58 + (-8 - (longlong)pplVar19)))))))) {
                    /* WARNING: Subroutine does not return */
          _invalid_parameter_noinfo_noreturn();
        }
        free(pplVar19);
      }
      uVar23 = 0;
LAB_18161e142:
      pcVar25 = "@ui_StashTrash";
      sVar17 = 0xffffffffffffffff;
      do {
        sVar20 = sVar17 + 1;
        lVar10 = sVar17 + 1;
        sVar17 = sVar20;
      } while ("@ui_StashTrash"[lVar10] != '\0');
    }
    else {
      (**(code **)(*plVar14 + 0x48))(plVar14,&local_58);
      pplVar5 = local_50;
      for (pplVar19 = local_58; pplVar19 != pplVar5;
          pplVar19 = (longlong **)((longlong)pplVar19 + 4)) {
        plVar14 = (longlong *)FUN_18117f3e0(*(undefined4 *)pplVar19);
        cVar6 = (**(code **)(*plVar14 + 0x170))(plVar14);
        if ((cVar6 != '\0') && (cVar6 = (**(code **)(*plVar14 + 0xe8))(plVar14), cVar6 != '\0')) {
          if (local_58 != (longlong **)0x0) {
            uVar16 = (longlong)local_48 - (longlong)local_58 >> 2;
            if ((0x3fffffffffffffff < uVar16) ||
               ((pplVar19 = local_58, 0xfff < uVar16 * 4 &&
                (((((ulonglong)local_58 & 0x1f) != 0 ||
                  (pplVar19 = (longlong **)local_58[-1], local_58 <= pplVar19)) ||
                 (0x1f < (ulonglong)((longlong)local_58 + (-8 - (longlong)pplVar19)))))))) {
                    /* WARNING: Subroutine does not return */
              _invalid_parameter_noinfo_noreturn();
            }
            free(pplVar19);
          }
          uVar23 = 1;
          goto LAB_18161df48;
        }
      }
      if (local_58 != (longlong **)0x0) {
        uVar16 = (longlong)local_48 - (longlong)local_58 >> 2;
        if ((0x3fffffffffffffff < uVar16) ||
           ((pplVar19 = local_58, 0xfff < uVar16 * 4 &&
            (((((ulonglong)local_58 & 0x1f) != 0 ||
              (pplVar19 = (longlong **)local_58[-1], local_58 <= pplVar19)) ||
             (0x1f < (ulonglong)((longlong)local_58 + (-8 - (longlong)pplVar19)))))))) {
                    /* WARNING: Subroutine does not return */
          _invalid_parameter_noinfo_noreturn();
        }
        free(pplVar19);
      }
      uVar23 = 0;
LAB_18161df48:
      pcVar25 = "@ui_TakeAll";
      sVar17 = 0xffffffffffffffff;
      do {
        sVar20 = sVar17 + 1;
        lVar10 = sVar17 + 1;
        sVar17 = sVar20;
      } while ("@ui_TakeAll"[lVar10] != '\0');
    }
    local_70 = &DAT_18224d03c;
    if (sVar20 != 0) {
      puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xd,0xf);
      *puVar12 = 1;
      local_70 = (char *)(puVar12 + 3);
      puVar12[1] = (int)sVar20;
      puVar12[2] = (int)sVar20;
      local_70[sVar20] = '\0';
      if (local_70 != pcVar25) {
        memcpy(local_70,pcVar25,sVar20);
      }
    }
    iVar8 = (int)local_res20;
    FUN_18163c5f0(local_68,(ulonglong)local_res20 & 0xffffffff,1,DAT_182c16838 + 0x768,&local_70,1,
                  uVar23);
    iVar8 = iVar8 + 1;
    local_res20 = (char *)CONCAT44(local_res20._4_4_,iVar8);
    iVar24 = *(int *)(local_70 + -0xc);
    if ((-1 < iVar24) && (iVar24 = iVar24 + -1, *(int *)(local_70 + -0xc) = iVar24, iVar24 < 1)) {
      FUN_1800a8b20();
    }
  }
  else {
LAB_18161e1c8:
    iVar8 = (int)local_res20;
  }
  uVar11 = local_68;
  if (local_78 != (longlong *)0x0) {
    local_88 = &DAT_18224d03c;
    if ((char)local_res18 == '\0') {
      if ((*(int *)(pcVar18 + -8) == 0) && (cVar6 = FUN_1810dce00(local_78,plVar3), cVar6 == '\0'))
      {
        pcVar25 = &DAT_18224d03c;
        sVar17 = 0xffffffffffffffff;
        do {
          sVar20 = sVar17;
          sVar17 = sVar20 + 1;
        } while ((&UNK_181c846b5)[sVar20] != '\0');
        if (sVar17 != 0) {
          puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
          *puVar12 = 1;
          pcVar25 = (char *)(puVar12 + 3);
          puVar12[1] = (int)sVar17;
          puVar12[2] = (int)sVar17;
          pcVar25[sVar17] = '\0';
          if (pcVar25 != " - ") {
            memcpy(pcVar25,&DAT_181c846b4,sVar17);
          }
        }
        if (*(int *)(pcVar25 + -0xc) < 0) {
          local_70 = &DAT_18224d03c;
        }
        else {
          *(int *)(pcVar25 + -0xc) = *(int *)(pcVar25 + -0xc) + 1;
          local_70 = pcVar25;
        }
        lVar10 = -1;
        do {
          lVar4 = lVar10 + 1;
          lVar10 = lVar10 + 1;
        } while ("@ui_InventoryFull"[lVar4] != '\0');
        FUN_1800a7b80(&local_70);
        pcVar21 = local_70;
        if (pcVar18 != local_70) {
          piVar1 = (int *)(pcVar18 + -0xc);
          if (*(int *)(pcVar18 + -0xc) < 0) {
            if (-1 < *(int *)(local_70 + -0xc)) {
LAB_18161e56a:
              *(int *)(pcVar21 + -0xc) = *(int *)(pcVar21 + -0xc) + 1;
              pcVar18 = pcVar21;
            }
          }
          else {
            iVar8 = *piVar1;
            if (-1 < *(int *)(local_70 + -0xc)) {
              if ((-1 < iVar8) && (*piVar1 = iVar8 + -1, iVar8 + -1 < 1)) {
                FUN_1800a8b20();
              }
              goto LAB_18161e56a;
            }
            pcVar18 = pcVar21;
            if ((-1 < iVar8) && (*piVar1 = iVar8 + -1, iVar8 + -1 < 1)) {
              FUN_1800a8b20();
            }
          }
        }
        if ((-1 < *(int *)(pcVar21 + -0xc)) &&
           (iVar8 = *(int *)(pcVar21 + -0xc) + -1, *(int *)(pcVar21 + -0xc) = iVar8, iVar8 < 1)) {
          FUN_1800a8b20();
        }
        if ((-1 < *(int *)(pcVar25 + -0xc)) &&
           (iVar8 = *(int *)(pcVar25 + -0xc) + -1, *(int *)(pcVar25 + -0xc) = iVar8, iVar8 < 1)) {
          FUN_1800a8b20(pcVar25 + -0xc);
        }
      }
      iVar8 = DAT_18224d034;
      local_70 = &DAT_18224d03c;
      pcVar25 = "@ui_Stash";
      lVar10 = -1;
      do {
        lVar15 = lVar10 + 1;
        lVar4 = lVar10 + 1;
        lVar10 = lVar15;
      } while ("@ui_Stash"[lVar4] != '\0');
      uVar16 = *(int *)(pcVar18 + -8) + lVar15;
      if ((ulonglong)(longlong)DAT_18224d038 < uVar16) {
        if (uVar16 == 0) {
          pcVar21 = &DAT_18224d03c;
          local_70 = &DAT_18224d03c;
        }
        else {
          puVar12 = (undefined4 *)FUN_1800a8b30(uVar16 + 0xd,0xf,1);
          *puVar12 = 1;
          pcVar21 = (char *)(puVar12 + 3);
          puVar12[1] = (int)uVar16;
          puVar12[2] = (int)uVar16;
          pcVar21[uVar16] = '\0';
          local_70 = pcVar21;
          if (pcVar21 != &DAT_18224d03c) {
            memcpy(pcVar21,&DAT_18224d03c,(longlong)DAT_18224d034);
          }
        }
        *(int *)(pcVar21 + -8) = DAT_18224d034;
        pcVar21[DAT_18224d034] = '\0';
LAB_18161e691:
        if ((-1 < DAT_18224d030) && (DAT_18224d030 = DAT_18224d030 + -1, DAT_18224d030 < 1)) {
          FUN_1800a8b20(&DAT_18224d030);
        }
      }
      else if ((uVar16 == 0) && (lVar10 = (longlong)DAT_18224d034, DAT_18224d034 != DAT_18224d038))
      {
        if (DAT_18224d034 == 0) {
          local_70 = &DAT_18224d03c;
        }
        else {
          puVar12 = (undefined4 *)FUN_1800a8b30(lVar10 + 0xd,0xf,1);
          *puVar12 = 1;
          local_70 = (char *)(puVar12 + 3);
          puVar12[1] = iVar8;
          puVar12[2] = iVar8;
          local_70[lVar10] = '\0';
          if (local_70 != &DAT_18224d03c) {
            memcpy(local_70,&DAT_18224d03c,(longlong)DAT_18224d034);
          }
        }
        goto LAB_18161e691;
      }
      do {
        sVar17 = sVar22 + 1;
        lVar10 = sVar22 + 1;
        sVar22 = sVar17;
      } while ("@ui_Stash"[lVar10] != '\0');
    }
    else {
      if ((*(int *)(pcVar18 + -8) == 0) &&
         (cVar6 = (**(code **)(*local_78 + 0xe8))(), cVar6 == '\0')) {
        pcVar25 = &DAT_18224d03c;
        sVar17 = 0xffffffffffffffff;
        do {
          sVar20 = sVar17;
          sVar17 = sVar20 + 1;
        } while ((&UNK_181c846b5)[sVar20] != '\0');
        if (sVar17 != 0) {
          puVar12 = (undefined4 *)FUN_1800a8b30(sVar20 + 0xe,0xf,1);
          *puVar12 = 1;
          pcVar25 = (char *)(puVar12 + 3);
          puVar12[1] = (int)sVar17;
          puVar12[2] = (int)sVar17;
          pcVar25[sVar17] = '\0';
          if (pcVar25 != " - ") {
            memcpy(pcVar25,&DAT_181c846b4,sVar17);
          }
        }
        if (*(int *)(pcVar25 + -0xc) < 0) {
          local_70 = &DAT_18224d03c;
        }
        else {
          *(int *)(pcVar25 + -0xc) = *(int *)(pcVar25 + -0xc) + 1;
          local_70 = pcVar25;
        }
        lVar10 = -1;
        do {
          lVar4 = lVar10 + 1;
          lVar10 = lVar10 + 1;
        } while ("@ui_InventoryFull"[lVar4] != '\0');
        FUN_1800a7b80(&local_70);
        pcVar21 = local_70;
        if (pcVar18 != local_70) {
          piVar1 = (int *)(pcVar18 + -0xc);
          if (*(int *)(pcVar18 + -0xc) < 0) {
            if (-1 < *(int *)(local_70 + -0xc)) {
LAB_18161e2da:
              *(int *)(pcVar21 + -0xc) = *(int *)(pcVar21 + -0xc) + 1;
              pcVar18 = pcVar21;
            }
          }
          else {
            iVar8 = *piVar1;
            if (-1 < *(int *)(local_70 + -0xc)) {
              if ((-1 < iVar8) && (*piVar1 = iVar8 + -1, iVar8 + -1 < 1)) {
                FUN_1800a8b20();
              }
              goto LAB_18161e2da;
            }
            pcVar18 = pcVar21;
            if ((-1 < iVar8) && (*piVar1 = iVar8 + -1, iVar8 + -1 < 1)) {
              FUN_1800a8b20();
            }
          }
        }
        if ((-1 < *(int *)(pcVar21 + -0xc)) &&
           (iVar8 = *(int *)(pcVar21 + -0xc) + -1, *(int *)(pcVar21 + -0xc) = iVar8, iVar8 < 1)) {
          FUN_1800a8b20();
        }
        if ((-1 < *(int *)(pcVar25 + -0xc)) &&
           (iVar8 = *(int *)(pcVar25 + -0xc) + -1, *(int *)(pcVar25 + -0xc) = iVar8, iVar8 < 1)) {
          FUN_1800a8b20(pcVar25 + -0xc);
        }
      }
      iVar8 = DAT_18224d034;
      local_70 = &DAT_18224d03c;
      pcVar25 = "@ui_Take";
      lVar10 = -1;
      do {
        lVar15 = lVar10 + 1;
        lVar4 = lVar10 + 1;
        lVar10 = lVar15;
      } while ("@ui_Take"[lVar4] != '\0');
      uVar16 = *(int *)(pcVar18 + -8) + lVar15;
      if ((ulonglong)(longlong)DAT_18224d038 < uVar16) {
        if (uVar16 == 0) {
          pcVar21 = &DAT_18224d03c;
          local_70 = &DAT_18224d03c;
        }
        else {
          puVar12 = (undefined4 *)FUN_1800a8b30(uVar16 + 0xd,0xf,1);
          *puVar12 = 1;
          pcVar21 = (char *)(puVar12 + 3);
          puVar12[1] = (int)uVar16;
          puVar12[2] = (int)uVar16;
          pcVar21[uVar16] = '\0';
          local_70 = pcVar21;
          if (pcVar21 != &DAT_18224d03c) {
            memcpy(pcVar21,&DAT_18224d03c,(longlong)DAT_18224d034);
          }
        }
        *(int *)(pcVar21 + -8) = DAT_18224d034;
        pcVar21[DAT_18224d034] = '\0';
LAB_18161e401:
        if ((-1 < DAT_18224d030) && (DAT_18224d030 = DAT_18224d030 + -1, DAT_18224d030 < 1)) {
          FUN_1800a8b20(&DAT_18224d030);
        }
      }
      else if ((uVar16 == 0) && (lVar10 = (longlong)DAT_18224d034, DAT_18224d034 != DAT_18224d038))
      {
        if (DAT_18224d034 == 0) {
          local_70 = &DAT_18224d03c;
        }
        else {
          puVar12 = (undefined4 *)FUN_1800a8b30(lVar10 + 0xd,0xf,1);
          *puVar12 = 1;
          local_70 = (char *)(puVar12 + 3);
          puVar12[1] = iVar8;
          puVar12[2] = iVar8;
          local_70[lVar10] = '\0';
          if (local_70 != &DAT_18224d03c) {
            memcpy(local_70,&DAT_18224d03c,(longlong)DAT_18224d034);
          }
        }
        goto LAB_18161e401;
      }
      do {
        sVar17 = sVar22 + 1;
        lVar10 = sVar22 + 1;
        sVar22 = sVar17;
      } while ("@ui_Take"[lVar10] != '\0');
    }
    FUN_1800a7b80(&local_70,pcVar25,sVar17);
    FUN_1800a7b80(&local_70,pcVar18,(longlong)*(int *)(pcVar18 + -8));
    pcVar25 = local_70;
    if (local_88 != local_70) {
      piVar1 = (int *)(local_88 + -0xc);
      if (*(int *)(local_88 + -0xc) < 0) {
        if (-1 < *(int *)(local_70 + -0xc)) {
LAB_18161e6f2:
          local_88 = pcVar25;
          *(int *)(pcVar25 + -0xc) = *(int *)(pcVar25 + -0xc) + 1;
        }
      }
      else {
        iVar8 = *piVar1;
        if (-1 < *(int *)(local_70 + -0xc)) {
          if ((-1 < iVar8) && (*piVar1 = iVar8 + -1, iVar8 + -1 < 1)) {
            FUN_1800a8b20();
          }
          goto LAB_18161e6f2;
        }
        if ((-1 < iVar8) && (*piVar1 = iVar8 + -1, iVar8 + -1 < 1)) {
          FUN_1800a8b20();
        }
        local_88 = pcVar25;
      }
    }
    if ((-1 < *(int *)(pcVar25 + -0xc)) &&
       (iVar8 = *(int *)(pcVar25 + -0xc) + -1, *(int *)(pcVar25 + -0xc) = iVar8, iVar8 < 1)) {
      FUN_1800a8b20();
    }
    uVar11 = local_68;
    iVar8 = (int)local_res20;
    FUN_18163c5f0(local_68,(ulonglong)local_res20 & 0xffffffff,1,DAT_182c16838 + 0x760,&local_88,0,
                  *(int *)(pcVar18 + -8) == 0);
    iVar8 = iVar8 + 1;
    iVar24 = *(int *)(local_88 + -0xc);
    if ((-1 < iVar24) && (iVar24 = iVar24 + -1, *(int *)(local_88 + -0xc) = iVar24, iVar24 < 1)) {
      FUN_1800a8b20();
    }
  }
  FUN_181636db0(uVar11,iVar8);
  if ((-1 < *(int *)(pcVar18 + -0xc)) &&
     (iVar8 = *(int *)(pcVar18 + -0xc) + -1, *(int *)(pcVar18 + -0xc) = iVar8, iVar8 < 1)) {
    FUN_1800a8b20();
  }
LAB_18161e791:
  FUN_18163d450(uVar11,param_2,local_60);
  return;
}

