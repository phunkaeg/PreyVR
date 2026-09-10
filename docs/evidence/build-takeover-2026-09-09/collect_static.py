from pathlib import Path
import json,urllib.request,urllib.parse
out=Path('docs/evidence/build-takeover-2026-09-09')
def get(endpoint, filename, **args):
    args['program']='/Prey/PreyDll.dll'
    url='http://127.0.0.1:8089'+endpoint+'?'+urllib.parse.urlencode(args)
    data=urllib.request.urlopen(url,timeout=15).read().decode('utf8')
    (out/filename).write_text(data,encoding='utf8')
    if not data.strip() or '"error"' in data[:150]: raise RuntimeError(filename+': '+data[:150])
    print(filename,len(data))
    return data
info=json.loads(get('/get_current_program_info','ghidra-program.json'))
assert info['path']=='/Prey/PreyDll.dll' and info['image_base']=='180000000' and info['address_size']==64
get('/get_program_options','ghidra-options.json',group='Program Information')
get('/decompile_function','rt-begin-frame.c',address='0x180F7D710')
get('/disassemble_function','rt-begin-frame.asm.txt',address='0x180F7D710')
get('/read_memory','rt-begin-frame-bytes.json',address='0x180F7D710',length=0x840)
get('/decompile_function','weapon-query-builder.c',address='0x18124C210')
get('/disassemble_function','weapon-query-builder.asm.txt',address='0x18124C210')
get('/decompile_function','melee-query-builder.c',address='0x18124BA90')
get('/read_memory','physics-accessor-bytes.json',address='0x180DF1720',length=9)
get('/read_memory','physics-accessor-slot.json',address='0x181D9BBD8',length=8)
get('/get_xrefs_to','environment-base-xrefs.txt',address='0x18224D980',limit=80)
