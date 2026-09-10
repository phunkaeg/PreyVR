
/* Steam CArkWeapon::AttachToHand, exact EGS/Steam byte/control-flow match. Consumes the weapon's
   resolved IAttachment pointer at this+0x2B0, installs a binding through attachment vtable slot
   +0xD8, then updates attachment/entity visibility and shadow-hand state. This is a viewmodel
   ownership lead, not yet a safe per-frame transform override. */

undefined8 CArkWeapon_AttachToHand(longlong param_1)

{
  int *piVar1;
  int iVar2;
  longlong *plVar3;
  longlong lVar4;
  char cVar5;
  undefined4 uVar6;
  uint uVar7;
  longlong *plVar8;
  undefined8 *puVar9;
  undefined8 *puVar10;
  undefined1 local_78 [8];
  longlong local_70;
  int local_68;
  longlong local_48;
  longlong *local_40;
  undefined8 local_18;
  
  plVar8 = DAT_18224da38;
  plVar3 = *(longlong **)(param_1 + 0x40);
  lVar4 = *DAT_18224da38;
  uVar6 = (**(code **)(*(longlong *)(param_1 + 8) + 0x1d8))(param_1 + 8);
  plVar8 = (longlong *)(**(code **)(lVar4 + 0x70))(plVar8,uVar6);
  if ((((plVar8 == (longlong *)0x0) ||
       (cVar5 = (**(code **)(*plVar8 + 0x68))(plVar8), cVar5 != '\0')) ||
      (plVar3 == (longlong *)0x0)) ||
     ((cVar5 = (**(code **)(*plVar3 + 0x68))(plVar3), cVar5 != '\0' ||
      (*(longlong *)(param_1 + 0x2b0) == 0)))) {
    return 0;
  }
  puVar10 = (undefined8 *)0x0;
  local_70 = 0;
  local_68 = 0;
  (**(code **)(*plVar3 + 0x1a8))(plVar3,0);
  (**(code **)(*(longlong *)(param_1 + 8) + 0x248))(param_1 + 8,1);
  (**(code **)(*plVar3 + 0x358))(plVar3,0);
  cVar5 = (**(code **)(*plVar3 + 0x280))(plVar3,0,local_78);
  if (cVar5 == '\0') {
    puVar9 = (undefined8 *)FUN_181b77290(0x18);
    if (puVar9 != (undefined8 *)0x0) {
      *(undefined4 *)(puVar9 + 1) = 0;
      *puVar9 = &PTR_FUN_181cb13b0;
      *(undefined4 *)((longlong)puVar9 + 0xc) = 0x3f800000;
      *(undefined4 *)(puVar9 + 2) = 0x3f800000;
      *(undefined4 *)((longlong)puVar9 + 0x14) = 0x3f800000;
      puVar10 = puVar9;
    }
    *(undefined4 *)(puVar10 + 1) = *(undefined4 *)(param_1 + 0x38);
  }
  else if (local_40 == (longlong *)0x0) {
    if (local_48 == 0) goto LAB_181691713;
    puVar9 = (undefined8 *)FUN_181b77290(0x18);
    if (puVar9 != (undefined8 *)0x0) {
      *puVar9 = &PTR_FUN_181cb12a0;
      puVar9[1] = 0;
      puVar9[2] = 0;
      puVar10 = puVar9;
    }
    FUN_1801a5cc0(puVar10 + 1,local_48);
    FUN_1801a5cc0(puVar10 + 2,local_18);
  }
  else {
    puVar9 = (undefined8 *)FUN_181b77290(0x18);
    if (puVar9 != (undefined8 *)0x0) {
      *puVar9 = &PTR_FUN_181cb1328;
      puVar9[1] = 0;
      puVar9[2] = 0;
      puVar10 = puVar9;
    }
    FUN_1801a5cc0(puVar10 + 1,local_40);
    if (local_40 != (longlong *)0x0) {
      lVar4 = *local_40;
      uVar7 = (**(code **)(lVar4 + 0xd0))(local_40);
      (**(code **)(lVar4 + 200))(local_40,uVar7 & 0xfffffffb);
    }
  }
  if (puVar10 != (undefined8 *)0x0) {
    (**(code **)(**(longlong **)(param_1 + 0x2b0) + 0xd8))
              (*(longlong **)(param_1 + 0x2b0),puVar10,0,0);
    (**(code **)(**(longlong **)(param_1 + 0x2b0) + 0x88))(*(longlong **)(param_1 + 0x2b0),0);
    (**(code **)(**(longlong **)(param_1 + 0x2b0) + 0xa8))(*(longlong **)(param_1 + 0x2b0),1);
  }
LAB_181691713:
  uVar7 = (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c8))(*(longlong **)(param_1 + 0x40),0);
  (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c0))
            (*(longlong **)(param_1 + 0x40),0,uVar7 & 0xfffffffe | 2);
  uVar7 = (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c8))(*(longlong **)(param_1 + 0x40),1);
  (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c0))
            (*(longlong **)(param_1 + 0x40),1,uVar7 & 0xfffffffc);
  uVar7 = (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c8))(*(longlong **)(param_1 + 0x40),8);
  (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c0))
            (*(longlong **)(param_1 + 0x40),8,uVar7 & 0xfffffffc);
  FUN_1816917f0(param_1,1);
  if ((local_70 != 0) && (local_68 < 0)) {
    LOCK();
    piVar1 = (int *)(local_70 + -4);
    iVar2 = *piVar1;
    *piVar1 = *piVar1 + -1;
    UNLOCK();
    if (iVar2 == 1) {
      free((void *)(local_70 + -4));
    }
  }
  return 1;
}


