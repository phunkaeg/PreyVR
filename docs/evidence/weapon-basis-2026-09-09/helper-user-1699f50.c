
void FUN_181699f50(longlong param_1)

{
  longlong *plVar1;
  undefined8 uVar2;
  undefined1 local_58 [32];
  undefined1 local_38 [48];
  
  if ((*(longlong *)(param_1 + 0x418) != 0) && (*(char *)(param_1 + 0x441) != '\0')) {
    uVar2 = Prey_GetEntitySlotHelperWorldTM
                      (local_38,*(undefined8 *)(param_1 + 0x40),0,*(undefined8 *)(param_1 + 0x410),1
                       ,0);
    FUN_1803ac620(local_58,uVar2);
    (**(code **)(**(longlong **)(param_1 + 0x418) + 0x28))
              (*(longlong **)(param_1 + 0x418),local_58,1);
    plVar1 = *(longlong **)(param_1 + 0x418);
    if (plVar1 != (longlong *)0x0) {
      (**(code **)(*plVar1 + 0x48))(plVar1,1);
    }
  }
  return;
}


