
int FUN_180822fb0(longlong *param_1,char *param_2)

{
  int iVar1;
  int iVar2;
  longlong *plVar3;
  char *_Str1;
  int iVar4;
  
  iVar1 = (**(code **)(*param_1 + 0x50))();
  iVar4 = 0;
  if (0 < iVar1) {
    do {
      plVar3 = (longlong *)(**(code **)(*param_1 + 0x40))(param_1,iVar4);
      _Str1 = (char *)(**(code **)(*plVar3 + 0x10))(plVar3);
      iVar2 = _stricmp(_Str1,param_2);
      if (iVar2 == 0) {
        return iVar4;
      }
      iVar4 = iVar4 + 1;
    } while (iVar4 < iVar1);
  }
  return -1;
}


