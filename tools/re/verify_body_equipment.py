"""Verify Steam equipment/vitals contracts offline; never opens a process."""
import hashlib,json,re,struct,sys
from pathlib import Path
from verify_h005_skinning import DEFAULT_MODULE,EXPECTED_SHA256
ROOT=Path(__file__).resolve().parents[2]
def verify():
 data=DEFAULT_MODULE.read_bytes();digest=hashlib.sha256(data).hexdigest()
 assert digest==EXPECTED_SHA256,digest
 u16=lambda p:struct.unpack_from('<H',data,p)[0]
 u32=lambda p:struct.unpack_from('<I',data,p)[0]
 pe=u32(0x3c);assert u16(pe+4)==0x8664 and u16(pe+24)==0x20b
 sections=[]
 for n in range(u16(pe+6)):
  off=pe+24+u16(pe+20)+n*40
  sections.append((u32(off+12),u32(off+16),u32(off+20)))
 def at(rva,count):
  for va,size,raw in sections:
   if va<=rva and rva+count<=va+size:return data[raw+rva-va:raw+rva-va+count]
  raise ValueError(hex(rva))
 def fnv(b):
  h=14695981039346656037
  for value in b:h=((h^value)*1099511628211)&((1<<64)-1)
  return h
 checks=[]
 text=(ROOT/'include/preyvr/EquipmentNative.h').read_text()
 entries=re.findall(r'\{0x([0-9A-F]+),(\d+),0x([0-9A-F]+)ull\}, // (\w+)',text)
 assert len(entries)==22
 for rva,size,expected,name in entries:
  b=at(int(rva,16),int(size));h=int(expected,16)
  assert fnv(b)==h,name
  mutated=bytes([b[0]^1])+b[1:]
  assert fnv(mutated)!=h,name+' mutation refusal'
  checks.append(name+' complete Steam body and negative control')
 def proof(name,rva,hexbytes):
  b=bytes.fromhex(hexbytes);assert at(rva,len(b))==b,name;checks.append(name)
 proof('health float result comes from extension +0x40',0x158b4e1,'f30f104040')
 proof('psi current float at component +0x400',0x15acfe0,'f30f108900040000')
 proof('psi maximum float at component +0x408',0x15acfa0,'f30f108908040000')
 proof('psi component accessor returns player component first pointer',0x10839f0,'488b01c3')
 proof('status owner accessor returns component +0xc0',0x1587ca0,'488b81c0000000c3')
 # The action receiver producer was independently mapped in the scope work.
 proof('player embeds input and passes owner',0x157a563,'488d8ee0080000488bd6e84e2c0100')
 proof('healthUpdate identifies native HUD producer',0x1e57be8,'6865616c746855706461746500')
 proof('armorUpdate identifies native HUD producer',0x1e28ec0,'61726d6f7255706461746500')
 return dict(target=str(DEFAULT_MODULE),sha256=digest,checks=checks,
  evidence='static byte contracts; no in-game or headset acceptance',game_launched=False)
if __name__=='__main__':
 result=json.dumps(verify(),indent=2)
 if len(sys.argv)>1:Path(sys.argv[1]).write_text(result+'\n',encoding='utf-8',newline='\n')
 print(result)
