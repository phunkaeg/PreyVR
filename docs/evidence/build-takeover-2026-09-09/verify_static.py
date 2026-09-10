from pathlib import Path
import hashlib,json,struct,pefile,re
root=Path.cwd(); out=root/'docs/evidence/build-takeover-2026-09-09'
target=Path('D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll')
raw=target.read_bytes(); digest=hashlib.sha256(raw).hexdigest()
assert digest=='7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'
pe=pefile.PE(data=raw,fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase
assert base==0x180000000 and pe.FILE_HEADER.Machine==0x8664
options=json.loads((out/'ghidra-options.json').read_text())
assert next(x['value'] for x in options['options'] if x['name']=='Executable SHA256').lower()==digest
checks=[]
for name in ['rt-begin-frame-bytes.json','system-constructor-bytes.json','weapon-query-builder-bytes.json','melee-query-builder-bytes.json','physics-accessor-bytes.json','physics-accessor-slot.json','renderer-begin-slot.json']:
    data=json.loads((out/name).read_text()); captured=bytes(data['data']); rva=int(data['address'],16)-base
    assert captured==pe.get_data(rva,len(captured)), name
    checks.append(dict(artifact=name,rva=hex(rva),bytes=len(captured),sha256=hashlib.sha256(captured).hexdigest()))
cpp=(root/'src/dll/NearFovOverride.cpp').read_text(encoding='utf-8-sig')
def array(name):
    body=re.search(name+r'\{(.*?)\};',cpp,re.S).group(1)
    return bytes(int(x.strip(),0) for x in body.split(',') if x.strip())
assert array('kBeginFramePrologue')==pe.get_data(0xF7D710,23)
assert array('kLatchInstructions')==pe.get_data(0xF7DC85,23)
assert 0xF7DC85+8+struct.unpack('<i',pe.get_data(0xF7DC89,4))[0]==0x2B1C64C
assert struct.unpack('<Q',pe.get_data(0x1DD2E08+0x8C8,8))[0]==base+0xF7D710
assert struct.unpack('<Q',pe.get_data(0x1D9B9C8+0x210,8))[0]==base+0xDF1720
assert pe.get_data(0xDF1720,9).hex()=='488b4128488b4048c3'
assert pe.get_data(0xDEF059,7).hex()=='488d0520e94501' # lea gEnv base
assert 0x224D980+0x48==0x224D9C8
result=dict(target=str(target),sha256=digest,image_base=hex(base),machine='x64',checks=checks,code_gate='PASS',receiver_chain='PASS',runtime='not performed')
(out/'static-verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
