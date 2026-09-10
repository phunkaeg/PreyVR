"""Find RIP-relative LEA candidates in hashed Steam bytes, plus concrete slots."""
import hashlib,json,re,struct
from pathlib import Path
import pefile

path=Path('D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll')
data=path.read_bytes()
assert hashlib.sha256(data).hexdigest()=='7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'
pe=pefile.PE(data=data)
targets={}
for name in ('inventoryDragPos','inventoryPickItem','inventoryPlaceItem'):
    offset=data.index(name.encode()+b'\0')
    targets[pe.get_rva_from_offset(offset)]=name
found=[]
for section in pe.sections:
    if not section.Characteristics&0x20000000: continue
    code=section.get_data()
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
        offset=m.start();rva=section.VirtualAddress+offset
        target=rva+7+struct.unpack_from('<i',code,offset+3)[0]
        if target in targets:
            owner=next((x.struct.BeginAddress for x in pe.DIRECTORY_ENTRY_EXCEPTION if x.struct.BeginAddress<=rva<x.struct.EndAddress),None)
            found.append(dict(name=targets[target],rva=hex(rva),pdata_begin=hex(owner) if owner else None))
slots=[]
for table,slot in ((0x1DB56D8,0x70),(0x1EB4B60,0x128),(0x1EB4B60,0x168)):
    slots.append(dict(table=hex(table),slot=hex(slot),callee=hex(struct.unpack('<Q',pe.get_data(table+slot,8))[0]-pe.OPTIONAL_HEADER.ImageBase)))
print(json.dumps(dict(xrefs=found,slots=slots),indent=2))
