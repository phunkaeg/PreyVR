longlong FUN_1802fcf30(longlong *param_1)

{
  char cVar1;
  int iVar2;
  longlong lVar3;
  undefined8 uVar4;
  undefined1 *puVar5;
  longlong *plVar6;
  undefined1 *local_res10;
  
  if (DAT_18247fd34 == 0) {
    return 0;
  }
  do {
    plVar6 = param_1;
    param_1 = (longlong *)plVar6[0x18];
  } while ((longlong *)plVar6[0x18] != (longlong *)0x0);
  if (plVar6[0xc] == 0) {
    lVar3 = (**(code **)(*DAT_18224da60 + 0x4b0))();
    plVar6[0xc] = lVar3;
    if (lVar3 != 0) {
      local_res10 = &DAT_18224d03c;
      lVar3 = plVar6[8];
      uVar4 = (**(code **)(*DAT_18247fd60 + 0x28))();
      FUN_1800c9f70(&local_res10,"%s/%s",uVar4,lVar3);
      puVar5 = local_res10;
      cVar1 = (**(code **)(*DAT_18224d9e8 + 0x210))(DAT_18224d9e8,local_res10,0);
      if (cVar1 == '\0') {
        FUN_1800944d0(&local_res10,plVar6 + 8);
        puVar5 = local_res10;
      }
      cVar1 = (**(code **)(*(longlong *)plVar6[0xc] + 8))((longlong *)plVar6[0xc],puVar5);
      if (cVar1 == '\0') {
        lVar3 = plVar6[8];
        uVar4 = (**(code **)(*plVar6 + 0x48))(plVar6);
        FUN_1802d6d10(2,"%s: Can\'t find flash file: \"%s\"",uVar4,lVar3);
        if ((undefined8 *)plVar6[0xc] != (undefined8 *)0x0) {
          (*(code *)**(undefined8 **)plVar6[0xc])();
          plVar6[0xc] = 0;
        }
      }
      else if (DAT_18247fd30 != 0) {
        uVar4 = (**(code **)(*plVar6 + 0x48))(plVar6);
        FUN_1802d6d10(0,"%s: Created BootStrapper for flash file: \"%s\"",uVar4,puVar5);
      }
      if ((-1 < *(int *)(puVar5 + -0xc)) &&
         (iVar2 = *(int *)(puVar5 + -0xc) + -1, *(int *)(puVar5 + -0xc) = iVar2, iVar2 < 1)) {
        FUN_1800a8b20();
      }
    }
  }
  return plVar6[0xc];
}
