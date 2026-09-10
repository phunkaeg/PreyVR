
/* PreyVR 2026-09-07 (static; confirmed same day per RE-H021-STATIC-VERIFICATION): CArkWeapon
   firing-position query (called by GLOO OnPreRender 0x169F420 before the reticle ray). Returns
   {byte fellBackToCamera, Vec3 pos}. Muzzle = Prey_GetEntitySlotHelperWorldTM(0x1811A5CF0, weapon
   entity this+0x40 or param_4, slot 0, helper name this+0x2F0, world) translation. this+0x2F0 is
   the string loaded from XML key sAmmoSpawnPointName (0x1E6CC68) by the parameter loader 0x1697B90,
   whose receiver is the SECONDARY interface at weapon+8 (secondary vtable 0x1E6C888 slot +0x250),
   storing at secondary+0x2E8 = weapon+0x2F0 (store chain 0x1697ED6..0x1697F5C). The same loader
   writes the camera test distances at secondary +0x3E8/+0x3EC = whole-weapon +0x3F0
   (m_spawnFromCameraTestDistance, 1.1f) / +0x3F4 (m_spawnBehindCameraDistance). Camera position
   from gEnv->pSystem vtable+0x388 (view camera matrix). Safety raycasts along the OWNER's forward
   (owner entity vtable+0x158): if blocked, returns the camera position instead of the muzzle.
   Consequence: once the weapon entity follows the hand bone, the projectile ORIGIN follows, subject
   to this camera fallback; the reticle ray origin (ArkPlayer +0x17D4) is a separate cache the aim
   takeover moves with aim.origin. */

undefined1 *
Prey_CArkWeaponGetFiringPosition
          (longlong param_1,undefined1 *param_2,undefined4 param_3,longlong param_4)

{
  float fVar1;
  bool bVar2;
  undefined4 uVar3;
  int iVar4;
  longlong lVar5;
  longlong *plVar6;
  float *pfVar7;
  undefined8 *puVar8;
  undefined1 uVar9;
  float fVar10;
  float fVar11;
  float fVar12;
  float fVar13;
  float fVar14;
  float fVar15;
  undefined8 local_res8;
  uint in_stack_fffffffffffffe10;
  float local_1e8;
  float local_1e4;
  float local_1e0;
  float local_1d8;
  float local_1d4;
  float local_1d0;
  float local_1c0;
  float local_1bc;
  float local_1b8;
  float local_1b0;
  float local_1ac;
  float local_1a8;
  undefined8 local_198;
  undefined8 uStack_190;
  undefined8 local_188;
  undefined1 local_168 [112];
  undefined1 local_f8 [208];
  
  if (param_4 == 0) {
    param_4 = *(longlong *)(param_1 + 0x40);
  }
  uVar9 = 0;
  lVar5 = Prey_GetEntitySlotHelperWorldTM
                    (&local_198,param_4,0,*(undefined8 *)(param_1 + 0x2f0),1,
                     in_stack_fffffffffffffe10 & 0xffffff00);
  fVar13 = *(float *)(lVar5 + 0xc);
  fVar14 = *(float *)(lVar5 + 0x1c);
  fVar15 = *(float *)(lVar5 + 0x2c);
  lVar5 = (**(code **)(*DAT_18224da60 + 0x388))();
  plVar6 = DAT_18224da38;
  local_1e0 = *(float *)(lVar5 + 0x2c);
  local_1e4 = *(float *)(lVar5 + 0x1c);
  local_1e8 = *(float *)(lVar5 + 0xc);
  lVar5 = *DAT_18224da38;
  uVar3 = (**(code **)(*(longlong *)(param_1 + 8) + 0x1d8))();
  plVar6 = (longlong *)(**(code **)(lVar5 + 0x70))(plVar6,uVar3);
  if (plVar6 == (longlong *)0x0) {
LAB_181694ede:
    local_res8 = 0;
    fVar10 = fVar13 - local_1e8;
    fVar11 = fVar14 - local_1e4;
    fVar12 = fVar15 - local_1e0;
    puVar8 = (undefined8 *)FUN_18124ac10(&local_1b0,plVar6,&local_res8);
    local_198 = *puVar8;
    uStack_190 = puVar8[1];
    local_188 = puVar8[2];
    local_1a8 = *(float *)(param_1 + 0x3f0);
    local_1b0 = fVar10 * local_1a8;
    local_1ac = fVar11 * local_1a8;
    local_1a8 = fVar12 * local_1a8;
    FUN_18124c210(local_168,&local_1e8,&local_1b0,local_res8,&local_198,param_3);
    iVar4 = (**(code **)(*DAT_18224d9c8 + 0x118))
                      (DAT_18224d9c8,local_168,"RayWorldIntersection(Game)",4);
    local_1c0 = local_1e8;
    local_1bc = local_1e4;
    local_1b8 = local_1e0;
    if (iVar4 == 0) goto LAB_181694fba;
  }
  else {
    pfVar7 = (float *)(**(code **)(*plVar6 + 0x158))(plVar6);
    fVar10 = *pfVar7;
    fVar11 = pfVar7[1];
    fVar12 = pfVar7[2];
    local_res8 = 0;
    puVar8 = (undefined8 *)FUN_18124ac10(&local_1d8,plVar6,&local_res8);
    bVar2 = false;
    local_198 = *puVar8;
    uStack_190 = puVar8[1];
    local_188 = puVar8[2];
    if (*(float *)(param_1 + 0x3f4) == 0.0) {
LAB_181694dab:
      fVar1 = *(float *)(param_1 + 0x3f4);
      local_1d8 = local_1e8 - fVar10 * fVar1;
      local_1bc = local_1e4 - fVar11 * fVar1;
      local_1b8 = local_1e0 - fVar12 * fVar1;
    }
    else {
      local_1d0 = -*(float *)(param_1 + 0x3f4);
      local_1d8 = fVar10 * local_1d0;
      local_1d4 = fVar11 * local_1d0;
      local_1d0 = fVar12 * local_1d0;
      FUN_18124c210(local_f8,&local_1e8,&local_1d8,local_res8,&local_198,param_3);
      iVar4 = (**(code **)(*DAT_18224d9c8 + 0x118))
                        (DAT_18224d9c8,local_f8,"RayWorldIntersection(Game)",4);
      if (iVar4 == 0) goto LAB_181694dab;
      bVar2 = true;
      local_1d8 = local_1e8;
      local_1b8 = local_1e0;
      local_1bc = local_1e4;
    }
    local_1c0 = local_1d8;
    local_1d8 = *(float *)(param_1 + 0x3f0);
    if (!bVar2) {
      local_1d8 = local_1d8 + *(float *)(param_1 + 0x3f4);
    }
    local_1d0 = fVar12 * local_1d8;
    local_1d4 = fVar11 * local_1d8;
    local_1d8 = fVar10 * local_1d8;
    local_1b0 = local_1d8;
    local_1ac = local_1d4;
    local_1a8 = local_1d0;
    FUN_18124c210(local_168,&local_1c0,&local_1b0,local_res8,&local_198,param_3);
    iVar4 = (**(code **)(*DAT_18224d9c8 + 0x118))
                      (DAT_18224d9c8,local_168,"RayWorldIntersection(Game)",4);
    if (iVar4 == 0) goto LAB_181694ede;
  }
  uVar9 = 1;
  fVar13 = local_1c0;
  fVar14 = local_1bc;
  fVar15 = local_1b8;
LAB_181694fba:
  *param_2 = uVar9;
  *(float *)(param_2 + 4) = fVar13;
  *(float *)(param_2 + 8) = fVar14;
  *(float *)(param_2 + 0xc) = fVar15;
  return param_2;
}

