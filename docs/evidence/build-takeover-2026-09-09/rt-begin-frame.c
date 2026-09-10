
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* CD3D9Renderer::RT_BeginFrame -- render-thread frame begin. Registry ID R-002.
   
   Build: PreyDll.dll SHA-256 7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7
   RVA:   0xF7D710   Confidence: observed
   
   NAME CORRECTED 2026-08-15. Previously called "BeginRendererScene", a name
   invented alongside its pair rather than taken from symbols. Recovered by reverse
   translation to EGS 0x180F51650, named FRT_BeginFrame at
   RenderDll/XRenderD3D9/DriverD3D.h:1049.
   
   Disambiguation note: this prologue matches TWICE in the EGS image, at 0xF51650
   and 0x141CAD0. The correct one was selected by spacing -- RT_EndFrame sits
   0xB00 bytes after RT_BeginFrame in both builds (Steam 0xF7D710/0xF7E210, EGS
   0xF51650/0xF52150). The rejected candidate is ArkCystoid::ProcessNearbyCystoids,
   confirming the choice. A generic MSVC prologue with no member offsets is a weak
   cross-build anchor; prefer an interior anchor if this is ever re-derived.
   
   ABI: void RT_BeginFrame(CD3D9Renderer* _this)
   
   Evidence:
    - Resolved live from vtable slot +0x8C8, adjacent to RT_EndFrame's +0x8D0, on
      the CD3D9Renderer vtable (R-037).
    - A one-second Cheat Engine capture recorded 103 RT_EndFrame entries against
      109 Present entries; near-equal cadence, not a proven one-to-one contract.
   
   Validation recipe:
    Confirm the exact prologue before use --
      48 8B C4 55 53 48 8D 68 A1 48 81 EC B8 00 00 00
    The headless build doctor pins these bytes.
   
   Caution: semantics are strong but this function has never been mutation-tested. */

void CD3D9Renderer_RT_BeginFrame(longlong param_1)

{
  uint *puVar1;
  undefined **ppuVar2;
  ulonglong uVar3;
  char cVar4;
  char cVar5;
  int iVar6;
  undefined4 uVar7;
  uint uVar8;
  int iVar9;
  longlong *plVar10;
  undefined8 uVar11;
  int *piVar12;
  uint uVar13;
  ulonglong uVar14;
  float fVar15;
  float local_98;
  undefined8 local_94;
  undefined1 local_88 [7];
  undefined1 local_81;
  undefined4 local_7c;
  undefined8 local_78;
  undefined1 local_70;
  undefined2 local_6f;
  undefined1 local_6d;
  undefined1 local_68 [7];
  undefined1 local_61;
  undefined4 local_5c;
  undefined8 local_58;
  undefined1 local_50;
  undefined2 local_4f;
  undefined1 local_4d;
  undefined1 local_48 [7];
  undefined1 local_41;
  undefined4 local_3c;
  undefined8 local_38;
  undefined1 local_30;
  undefined2 local_2f;
  undefined1 local_2d;
  
  uVar14 = 0;
  iVar9 = 0;
  if ((DAT_182b302c0 == (longlong *)0x0) &&
     (DAT_182b302c0 = (longlong *)(**(code **)(*DAT_182b24e08 + 0xb8))(DAT_182b24e08,"e_Shadows"),
     DAT_182b302c0 == (longlong *)0x0)) {
LAB_180f7d76f:
    uVar8 = 0;
  }
  else {
    iVar6 = (**(code **)(*DAT_182b302c0 + 0x10))(DAT_182b302c0);
    uVar8 = 0x100;
    if (iVar6 == 0) goto LAB_180f7d76f;
  }
  *(uint *)(param_1 + 0x8088) = *(uint *)(param_1 + 0x8088) & 0xfffffeff;
  *(uint *)(param_1 + 0x8088) = *(uint *)(param_1 + 0x8088) | uVar8;
  local_98 = 0.0;
  local_94 = 0;
  if (DAT_18224d988 != (longlong *)0x0) {
    (**(code **)(*DAT_18224d988 + 0x4f0))(DAT_18224d988,0x10,&local_98);
  }
  plVar10 = DAT_182b302c8;
  if ((((*(uint *)(param_1 + 0x8088) >> 8 & 1) == 0) || (DAT_182b1c778 == 0)) ||
     (uVar8 = 0x200, local_98 == 0.0)) {
    uVar8 = 0;
  }
  *(uint *)(param_1 + 0x8088) = *(uint *)(param_1 + 0x8088) & 0xfffffdff | uVar8;
  if (((plVar10 == (longlong *)0x0) &&
      (plVar10 = (longlong *)(**(code **)(*DAT_182b24e08 + 0xb8))(DAT_182b24e08,"e_VolumetricFog"),
      DAT_182b302c8 = plVar10, plVar10 == (longlong *)0x0)) ||
     ((iVar9 = (**(code **)(*plVar10 + 0x10))(), iVar9 == 0 ||
      (cVar4 = FUN_180fc2470(), DAT_182b1c80c = iVar9, cVar4 == '\0')))) {
    DAT_182b1c80c = 0;
  }
  if (((DAT_182b302c8 != (longlong *)0x0) && (iVar9 != 0)) && (DAT_182b1c228 == 0)) {
    (**(code **)(*DAT_182b302c8 + 0x38))(DAT_182b302c8,0);
  }
  *(undefined4 *)(param_1 + 0x805c) = DAT_182b1c8dc;
  FUN_180ecec10(param_1);
  uVar11 = FUN_180fd9d20();
  FUN_180f72de0(uVar11);
  FUN_180ff1400();
  FUN_18107ed20(param_1 + 0x46e0,
                *(undefined4 *)((ulonglong)*(uint *)(param_1 + 0x499c) * 0x328 + 0x4c94 + param_1),0
               );
  FUN_180f639b0(param_1 + 0xb340);
  if (*(longlong *)(param_1 + 0xa138) != 0) {
    FUN_180fc9e20();
  }
  plVar10 = (longlong *)(**(code **)(*DAT_18224da40 + 0xb8))(DAT_18224da40,"e_texeldensity");
  if (plVar10 == (longlong *)0x0) {
    _DAT_182b1c604 = 0;
  }
  else {
    _DAT_182b1c604 = (**(code **)(*plVar10 + 0x10))(plVar10);
  }
  plVar10 = (longlong *)(**(code **)(*DAT_18224da40 + 0xb8))(DAT_18224da40,"e_debugrendermode");
  if (plVar10 == (longlong *)0x0) {
    _DAT_182b1c620 = 0;
  }
  else {
    _DAT_182b1c620 = (**(code **)(*plVar10 + 0x10))(plVar10);
  }
  FUN_180fdc930(*(undefined8 *)((ulonglong)*(uint *)(param_1 + 0x499c) * 0x328 + 0x49b8 + param_1));
  FUN_180f77f00(param_1);
  if (((*(char *)(param_1 + 0xd84) == '\0') && ((*(byte *)(param_1 + 0x8088) & 0x40) != 0)) &&
     ((DAT_182b1c730 + *(float *)(param_1 + 0x80c8) != *(float *)(param_1 + 0x80bc) ||
      ((DAT_182b1c738 != *(float *)(param_1 + 0x80c0) ||
       (DAT_182b1c734 != *(float *)(param_1 + 0x80c4))))))) {
    FUN_180f539e0(param_1);
  }
  if ((*(char *)(param_1 + 0x8084) == '\0') && (DAT_182b1c40c != 0)) {
    (**(code **)(*DAT_182b24e00 + 0x18))
              (DAT_182b24e00,"Device doesn\'t support HW geometry instancing (or it\'s disabled)");
    plVar10 = (longlong *)(**(code **)(*DAT_182b24e08 + 0xb8))(DAT_182b24e08,"r_GeomInstancing");
    if (plVar10 != (longlong *)0x0) {
      (**(code **)(*plVar10 + 0x38))(plVar10,0);
    }
  }
  if (DAT_182b1c580 != (*(uint *)(param_1 + 0x8088) >> 1 & 1)) {
    *(uint *)(param_1 + 0x8088) =
         -(uint)(DAT_182b1c580 != 0) & 2 | *(uint *)(param_1 + 0x8088) & 0xfffffffd;
    for (ppuVar2 = DAT_18227c448; ppuVar2 != &PTR_PTR_18227c440; ppuVar2 = (undefined **)ppuVar2[1])
    {
      cVar4 = (**(code **)(*ppuVar2 + 0x48))(ppuVar2);
      if (cVar4 != '\0') {
        (**(code **)(*ppuVar2 + 0x20))(ppuVar2);
      }
    }
  }
  iVar6 = FUN_180fe3760(param_1);
  iVar9 = DAT_182b1c2a4;
  fVar15 = 0.0;
  if (iVar6 == 0x10) {
    fVar15 = DAT_182b1c768;
  }
  if ((DAT_182b1c2a4 != *(char *)(param_1 + 0x8094)) || (fVar15 != *(float *)(param_1 + 0x8098))) {
    *(float *)(param_1 + 0x8098) = fVar15;
    *(char *)(param_1 + 0x8094) = (char)iVar9;
    uVar8 = DAT_18227e408;
    if (DAT_18227e408 != 0) {
      do {
        if (*(longlong *)(DAT_18227e400 + uVar14 * 8) != 0) {
          FUN_180eeb270();
          uVar8 = DAT_18227e408;
        }
        uVar13 = (int)uVar14 + 1;
        uVar14 = (ulonglong)uVar13;
      } while (uVar13 < uVar8);
    }
    cVar4 = (char)DAT_182b1c2a4;
    if ((char)DAT_182b1c2a4 < '\x10') {
      if ((char)DAT_182b1c2a4 < '\b') {
        if ((char)DAT_182b1c2a4 < '\x04') {
          cVar5 = ('\x01' < (char)DAT_182b1c2a4) + '\x03';
        }
        else {
          cVar5 = '\x05';
        }
      }
      else {
        cVar5 = '\x06';
      }
    }
    else {
      cVar5 = '\a';
    }
    local_38 = 0;
    FUN_180f58fb0(local_48,cVar5);
    FUN_180f58d20(local_48,0,0,0);
    FUN_180f58ce0(local_48,0);
    local_3c = 0;
    local_2f = 0;
    local_30 = 0;
    local_41 = 0;
    local_2d = 0;
    if (cVar4 < '\x10') {
      if (cVar4 < '\b') {
        if (cVar4 < '\x04') {
          cVar5 = ('\x01' < cVar4) + '\x03';
        }
        else {
          cVar5 = '\x05';
        }
      }
      else {
        cVar5 = '\x06';
      }
    }
    else {
      cVar5 = '\a';
    }
    local_58 = 0;
    FUN_180f58fb0(local_68,cVar5);
    FUN_180f58d20(local_68,0,0,0);
    FUN_180f58ce0(local_68,0);
    local_5c = 0;
    local_4f = 0;
    local_50 = 0;
    local_61 = 0;
    local_4d = 0;
    if (cVar4 < '\x10') {
      if (cVar4 < '\b') {
        if (cVar4 < '\x04') {
          cVar4 = ('\x01' < cVar4) + '\x03';
        }
        else {
          cVar4 = '\x05';
        }
      }
      else {
        cVar4 = '\x06';
      }
    }
    else {
      cVar4 = '\a';
    }
    local_78 = 0;
    FUN_180f58fb0(local_88,cVar4);
    FUN_180f58d20(local_88,3,3,3);
    FUN_180f58ce0(local_88,0);
    local_7c = 0;
    local_6f = 0;
    local_70 = 0;
    local_81 = 0;
    local_6d = 0;
    FUN_180f590f0(local_48,fVar15);
    FUN_180f590f0(local_68,fVar15);
    FUN_180f590f0(local_88,fVar15);
    uVar7 = FUN_180eaafb0(local_48);
    *(undefined4 *)(param_1 + 0x99a4) = uVar7;
    uVar7 = FUN_180eaafb0(local_68);
    *(undefined4 *)(param_1 + 0x99a8) = uVar7;
    uVar7 = FUN_180eaafb0(local_88);
    *(undefined4 *)(param_1 + 0x99ac) = uVar7;
    FUN_180f547e0(local_88);
    FUN_180f547e0(local_68);
    FUN_180f547e0(local_48);
  }
  *(undefined4 *)(param_1 + 0x95b4) = DAT_182b1c64c;
  FUN_180f52f60(param_1 + 0xaf80);
  FUN_180f7bc20(param_1);
  iVar9 = DAT_182b1c5d8;
  if (((DAT_182b1c5d8 == *(int *)(param_1 + 0x95a8)) ||
      (*(int *)(param_1 + 0x95a8) == DAT_182b1c5d8)) ||
     (*(int *)(param_1 + 0x95a8) = DAT_182b1c5d8, 0 < *(int *)(param_1 + 0x805c)))
  goto LAB_180f7dd10;
  *(uint *)(param_1 + 0x54d4) = *(uint *)(param_1 + 0x54d4) & 0xfffcffff;
  if (iVar9 == 2) {
    uVar8 = *(uint *)(param_1 + 0x54d4) | 0x20000;
LAB_180f7dcf8:
    *(uint *)(param_1 + 0x54d4) = uVar8;
  }
  else if (iVar9 == 1) {
    uVar8 = *(uint *)(param_1 + 0x54d4) | 0x10000;
    goto LAB_180f7dcf8;
  }
  FUN_180fe7510(param_1,*(undefined4 *)(param_1 + 0x54d0),0xffffffff);
LAB_180f7dd10:
  FUN_180f392a0(param_1,0xff,0xff,0x3a,0x3a);
  if (((*(float *)(param_1 + 0x4868) != 1.0) || (*(float *)(param_1 + 0x486c) != 1.0)) ||
     ((*(float *)(param_1 + 0x4870) != 1.0 || (*(float *)(param_1 + 0x4874) != 1.0)))) {
    *(undefined4 *)(param_1 + 0x4868) = 0x3f800000;
    *(undefined4 *)(param_1 + 0x486c) = 0x3f800000;
    *(undefined4 *)(param_1 + 0x4870) = 0x3f800000;
    *(undefined4 *)(param_1 + 0x4874) = 0x3f800000;
    puVar1 = (uint *)((ulonglong)*(uint *)(param_1 + 0x499c) * 0x328 + 0x49a0 + param_1);
    *puVar1 = *puVar1 | 0x200000;
  }
  FUN_180f7ee10(param_1);
  if (*(int *)(param_1 + 0xaef8) == 0) {
    *(undefined4 *)(param_1 + 0xaef8) = 1;
  }
  if ((DAT_182b1c5d8 != 0) || (DAT_182b1c308 == 0)) {
    FUN_180f0d270(param_1);
  }
  *(undefined4 *)(param_1 + 0x8080) = 1;
  cVar4 = FUN_180fe91a0();
  if (cVar4 == '\0') {
    memset((void *)(((ulonglong)*(uint *)(param_1 + 0x499c) + 0x6d) * 0x100 + param_1),0,0x100);
    uVar3 = *(ulonglong *)(param_1 + 0x6f00);
    uVar14 = uVar3 + (ulonglong)(*(uint *)(uVar3 - 4) & 0x7fffffff) * 0x18;
    while (uVar3 < uVar14) {
      plVar10 = (longlong *)(uVar14 - 0x18);
      uVar14 = uVar14 - 0x18;
      piVar12 = (int *)(*plVar10 + -0xc);
      iVar9 = *piVar12;
      if ((-1 < iVar9) && (iVar9 = iVar9 + -1, *piVar12 = iVar9, iVar9 < 1)) {
        FUN_1800a8b20();
      }
    }
    FUN_180f814d0(param_1 + 0x6f00,0,0);
    _DAT_182b3e930 = ((ulonglong)*(uint *)(param_1 + 0x499c) + 0x6d) * 0x100 + param_1;
  }
  *(undefined4 *)(param_1 + 0x9890) = 0;
  DAT_182b302d0 =
       (DAT_182b302d0 * 5.0 +
       *(float *)(param_1 + 0x8028 + (ulonglong)*(uint *)(param_1 + 0x4998) * 4)) * 0.16666667;
  if (DAT_182b302d0 < 0.004) {
    *(undefined4 *)(param_1 + 0x8090) = 0;
  }
  else if (*(uint *)(param_1 + 0x8090) < 1000) {
    *(uint *)(param_1 + 0x8090) = *(uint *)(param_1 + 0x8090) + 1;
  }
  *(bool *)((ulonglong)*(uint *)(param_1 + 0x499c) + 0x808d + param_1) =
       10 < *(uint *)(param_1 + 0x8090);
  if (DAT_182b1c254 == 0) {
    *(undefined1 *)((ulonglong)*(uint *)(param_1 + 0x499c) + 0x808d + param_1) = 0;
    return;
  }
  if (DAT_182b1c254 == 1) {
    *(undefined1 *)((ulonglong)*(uint *)(param_1 + 0x499c) + 0x808d + param_1) = 1;
  }
  return;
}

