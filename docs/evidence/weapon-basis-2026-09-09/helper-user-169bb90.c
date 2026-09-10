
void FUN_18169bb90(longlong param_1,char param_2)

{
  longlong *plVar1;
  longlong *plVar2;
  undefined8 *puVar3;
  undefined4 *puVar4;
  undefined8 uVar5;
  undefined8 local_78;
  undefined4 local_70;
  undefined8 local_6c;
  undefined4 local_64;
  undefined8 local_60 [4];
  undefined1 local_40 [56];
  
  if ((*(longlong *)(param_1 + 0x418) != 0) && (*(char *)(param_1 + 0x441) != '\0')) {
    plVar2 = *(longlong **)(param_1 + 0x40);
    if (param_2 == '\0') {
      uVar5 = Prey_GetEntitySlotHelperWorldTM
                        (local_40,plVar2,0,*(undefined8 *)(param_1 + 0x410),1,0);
      FUN_1803ac620(local_60,uVar5);
      (**(code **)(**(longlong **)(param_1 + 0x418) + 0x28))
                (*(longlong **)(param_1 + 0x418),local_60,1);
    }
    else {
      plVar2 = (longlong *)(**(code **)(*plVar2 + 0x2d8))(plVar2,0);
      if (plVar2 != (longlong *)0x0) {
        plVar2 = (longlong *)(**(code **)(*plVar2 + 0x40))(plVar2);
        plVar2 = (longlong *)(**(code **)(*plVar2 + 0x38))(plVar2,*(undefined8 *)(param_1 + 0x410));
        if (plVar2 != (longlong *)0x0) {
          puVar3 = (undefined8 *)FUN_181b77290(0x40);
          if (puVar3 == (undefined8 *)0x0) {
            puVar3 = (undefined8 *)0x0;
          }
          else {
            plVar1 = *(longlong **)(param_1 + 0x418);
            *puVar3 = &PTR_FUN_181cbb160;
            puVar3[1] = 0;
            puVar3[2] = 0;
            local_78 = 0;
            local_70 = 0;
            local_6c = 0;
            local_64 = 0;
            puVar4 = (undefined4 *)FUN_180139ce0(local_60,&local_6c,&local_78,0x3f800000);
            *(undefined4 *)((longlong)puVar3 + 0x24) = puVar4[3];
            *(undefined4 *)(puVar3 + 3) = *puVar4;
            *(undefined4 *)((longlong)puVar3 + 0x1c) = puVar4[1];
            *(undefined4 *)(puVar3 + 4) = puVar4[2];
            *(undefined4 *)(puVar3 + 5) = puVar4[4];
            *(undefined4 *)((longlong)puVar3 + 0x2c) = puVar4[5];
            *(undefined4 *)(puVar3 + 6) = puVar4[6];
            *(undefined4 *)((longlong)puVar3 + 0x34) = puVar4[7];
            *(undefined1 *)(puVar3 + 7) = 0;
            if (plVar1 != (longlong *)0x0) {
              (**(code **)(*plVar1 + 8))(plVar1);
            }
            if ((longlong *)puVar3[2] != (longlong *)0x0) {
              (**(code **)(*(longlong *)puVar3[2] + 0x10))();
            }
            puVar3[2] = plVar1;
          }
          (**(code **)(*plVar2 + 0xd8))(plVar2,puVar3,0,0);
        }
      }
    }
    plVar2 = *(longlong **)(param_1 + 0x418);
    if (plVar2 != (longlong *)0x0) {
      (**(code **)(*plVar2 + 0x48))(plVar2,1);
    }
  }
  return;
}


