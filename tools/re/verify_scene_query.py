"""Read-only Steam scene-query contract verification, with byte-mutation controls."""
import hashlib,json,re,struct,sys
from pathlib import Path
from verify_h005_skinning import DEFAULT_MODULE,EXPECTED_SHA256
ROOT=Path(__file__).resolve().parents[2]

def verify():
    data=DEFAULT_MODULE.read_bytes()
    digest=hashlib.sha256(data).hexdigest()
    assert digest==EXPECTED_SHA256,digest
    u16=lambda p:struct.unpack_from('<H',data,p)[0]
    u32=lambda p:struct.unpack_from('<I',data,p)[0]
    pe=u32(0x3c)
    assert u16(pe+4)==0x8664 and u16(pe+24)==0x20b
    sections=[]
    for n in range(u16(pe+6)):
        p=pe+24+u16(pe+20)+n*40
        sections.append((u32(p+12),u32(p+16),u32(p+20)))
    def at(rva,count):
        for va,size,raw in sections:
            if va<=rva and rva+count<=va+size:
                result=data[raw+rva-va:raw+rva-va+count]
                assert len(result)==count
                return result
        raise ValueError(hex(rva))
    def fnv(b):
        h=14695981039346656037
        for value in b:h=((h^value)*1099511628211)&((1<<64)-1)
        return h
    entries=re.findall(r'\{0x([0-9A-F]+),(\d+),0x([0-9A-F]+)ull\}, // (\w+)',
        (ROOT/'include/preyvr/SceneQueryNative.h').read_text())
    assert len(entries)==7
    checks=[]
    for rva,size,expected,name in entries:
        b=at(int(rva,16),int(size));expected=int(expected,16)
        assert fnv(b)==expected,name
        assert fnv(bytes([b[0]^1])+b[1:])!=expected,name+' negative control'
        checks.append(name+': full body matches; one-bit mutation refused')
    assert struct.unpack('<Q',at(0x1d82e70+0x118,8))[0]==0x180ccf250
    checks.append('Concrete world vtable +0x118 points to Steam 0xCCF250')
    def proof(name,rva,expected):
        b=bytes.fromhex(expected);assert at(rva,len(b))==b,name;checks.append(name)
    proof('Constructor installs concrete world vtable',0xb9e7b9,'488d05b0461e014533f6488907')
    proof('Native melee obtains player entity at +0x38',0x13bd677,'488b7838')
    proof('Native melee calls entity physics slot +0x228',0x13bd68b,'41ff9028020000')
    proof('Synchronous caller lock',0xccf945,'f0460fb1acb774570000')
    proof('Axis permutation tables used by indexed grid traversal',0x22614a8,
        '0100000002000000000000000000000002000000000000000100000000000000')
    return dict(target=str(DEFAULT_MODULE),sha256=digest,checks=checks,
        environment='static',game_launched=False,
        limits='Proves native bytes/layout/call route, not query acceptance, hit filtering in a scene, rendered reticle depth or performance.')

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
