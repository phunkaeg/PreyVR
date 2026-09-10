"""Read-only target identity, captured bytes and concrete accessor checks."""
from pathlib import Path
import hashlib, json, struct
import pefile

out = Path(__file__).resolve().parent
target = Path('D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll')
raw = target.read_bytes()
digest = hashlib.sha256(raw).hexdigest()
assert digest == '7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'
pe = pefile.PE(data=raw, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
assert base == 0x180000000 and pe.FILE_HEADER.Machine == 0x8664
options = json.loads((out/'target-options.json').read_text())
assert next(x['value'] for x in options['options'] if x['name'] == 'Executable SHA256') == digest
checks = []
for path in sorted(out.glob('*.json')):
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or not {'address', 'data', 'length'} <= data.keys():
        continue
    captured = bytes(data['data'])
    rva = int(data['address'], 16) - base
    assert captured == pe.get_data(rva, len(captured)), path.name
    checks.append({'artifact': path.name, 'rva': hex(rva), 'bytes': len(captured)})
assert len(checks) == 11, 'missing captured-byte positive controls'
slots = [(0x1D22110, 0x38, 0x8230D0), (0x1D22110, 0x40, 0x8230A0),
         (0x1D22110, 0x50, 0x82DD50), (0x1D22110, 0x58, 0x822FB0),
         (0x1CB1328, 0x10, 0x334EC0), (0x1D22200, 0x198, 0x82E760),
         (0x1D22200, 0x48, 0x12EABB0), (0x1D22200, 0x58, 0x8CE820),
         (0x1D2A3E8, 0x10, 0x8BC330), (0x1D2A3E8, 0x38, 0x8BC240),
         (0x1D2A3E8, 0x48, 0x8BC0E0), (0x1D27228, 0x48, 0x87C6D0)]
for table, offset, callee in slots:
    assert struct.unpack('<Q', pe.get_data(table+offset, 8))[0] == base+callee, (table,offset,callee)
assert pe.get_data(0x12EABB0, 5).hex() == '488d4118c3'
assert pe.get_data(0x8CE820, 5).hex() == '488b4110c3'
assert pe.get_data(0x334EC0, 14).hex() == '488b4908488b0148ffa098010000'
assert pe.get_data(0x87C6D0, 11).hex() == '8bc2486bc01c48034118c3'
result = {'target': str(target), 'sha256': digest, 'machine':'x64', 'image_base':hex(base),
          'captured_spans':checks, 'concrete_slots_checked':len(slots),
          'result':'PASS', 'runtime':'not performed'}
(out/'static-verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
