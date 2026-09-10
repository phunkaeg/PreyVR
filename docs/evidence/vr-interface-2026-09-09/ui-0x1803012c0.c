void FUN_1803012c0(longlong *param_1)

{
  longlong lVar1;
  char cVar2;
  
  cVar2 = (**(code **)(*param_1 + 0xf8))(param_1,1);
  if (((cVar2 == '\0') || ((char)param_1[0xe] == '\0')) ||
     (*(char *)((longlong)param_1 + 0x51) != '\0')) {
    if ((char)param_1[0x10] != '\0') {
      *(undefined1 *)(param_1 + 0x10) = 0;
      (**(code **)(*DAT_18224dab0 + 0x48))();
    }
  }
  else if ((char)param_1[0x10] == '\0') {
    *(undefined1 *)(param_1 + 0x10) = 1;
    (**(code **)(*DAT_18224dab0 + 0x40))();
  }
  if ((longlong *)param_1[0xb] != (longlong *)0x0) {
    lVar1 = *(longlong *)param_1[0xb];
    cVar2 = (**(code **)(*param_1 + 0xf8))(param_1,0x4000);
    (**(code **)(lVar1 + 0xa0))(param_1[0xb],cVar2 == '\0');
  }
  *(undefined2 *)((longlong)param_1 + 0x81) = 1;
  return;
}
