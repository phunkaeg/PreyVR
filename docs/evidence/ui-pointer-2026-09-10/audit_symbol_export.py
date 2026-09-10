"""Read-only identity and index audit of the extracted EGS reference corpus."""
import collections
import csv
import hashlib
import json
import mmap
from pathlib import Path
import sqlite3
import struct
import uuid

root = Path(__file__).resolve().parents[3] / '[WIN] Prey [2021-08-19]'
dll = (root / 'PreyDll.dll').read_bytes()
pe = struct.unpack_from('<I', dll, 0x3c)[0]
machine, sections = struct.unpack_from('<HH', dll, pe+4)
opt = pe+24
optional_size = struct.unpack_from('<H', dll, pe+20)[0]
def raw(rva):
    for i in range(sections):
        vs, va, size, offset = struct.unpack_from('<IIII', dll, opt+optional_size+i*40+8)
        if va <= rva < va+size:
            return offset+rva-va
    raise ValueError(hex(rva))
debug_rva, debug_size = struct.unpack_from('<II', dll, opt+112+6*8)
rsds = []
for offset in range(raw(debug_rva), raw(debug_rva)+debug_size, 28):
    _, _, _, _, kind, size, _, ptr = struct.unpack_from('<IIHHIIII', dll, offset)
    if kind == 2 and dll[ptr:ptr+4] == b'RSDS':
        rsds.append(dict(guid=str(uuid.UUID(bytes_le=dll[ptr+4:ptr+20])),
            age=struct.unpack_from('<I', dll, ptr+20)[0],
            path=dll[ptr+24:ptr+size].split(b'\0')[0].decode('utf-8', 'replace')))
with (root/'PreyDll.pdb').open('rb') as handle, mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ) as pdb:
    magic=b'Microsoft C/C++ MSF 7.00\r\n'
    assert pdb[:len(magic)] == magic
    block_size, _, _, directory_bytes, _, block_map = struct.unpack_from('<6I', pdb, 32)
    n=(directory_bytes+block_size-1)//block_size
    blocks=struct.unpack_from(f'<{n}I', pdb, block_map*block_size)
    directory=b''.join(pdb[b*block_size:(b+1)*block_size] for b in blocks)[:directory_bytes]
    count=struct.unpack_from('<I', directory)[0]
    sizes=struct.unpack_from(f'<{count}I', directory, 4)
    cursor=4+4*count
    streams={}
    for i,size in enumerate(sizes):
        n=0 if size==0xffffffff else (size+block_size-1)//block_size
        pages=struct.unpack_from(f'<{n}I', directory, cursor)
        cursor+=n*4
        if i in (1,2,3,4):
            streams[i]=b''.join(pdb[b*block_size:(b+1)*block_size] for b in pages)[:size]
    identity=dict(guid=str(uuid.UUID(bytes_le=streams[1][12:28])),age=struct.unpack_from('<I',streams[1],8)[0])
    type_start,type_end=struct.unpack_from('<II',streams[2],8)
    module_bytes,_,_,source_bytes=struct.unpack_from('<4I',streams[3],24)
    pdb_hash=hashlib.sha256(pdb).hexdigest()
corpus=root/'ida_decompile/PreyDll'
with (corpus/'index.csv').open(encoding='utf-8-sig',newline='') as handle:
    rows=list(csv.DictReader(handle))
missing=[]
for row in rows:
    alternatives=[row['file'],row['fallback_file']]
    if not any(relative and (corpus/relative).is_file() for relative in alternatives):
        missing.append(row['ea'])
with sqlite3.connect((corpus/'readable/navigation.sqlite').as_uri()+'?mode=ro',uri=True) as connection:
    counts={table:connection.execute(f'SELECT count(*) FROM {table}').fetchone()[0]
        for table in ('functions','calls')}
report=dict(donor='EGS/Chairloader reference; not the supported Steam target',
    dll_sha256=hashlib.sha256(dll).hexdigest(),machine=hex(machine),
    pe_codeview=rsds,pdb_identity=identity,pdb_matches_pe=any(all(r[k]==identity[k] for k in identity) for r in rsds),
    pdb_sha256=pdb_hash,pdb_type_records=type_end-type_start,
    pdb_module_info_bytes=module_bytes,pdb_source_info_bytes=source_bytes,
    indexed_functions=len(rows),statuses=dict(collections.Counter(r['status'] for r in rows)),
    missing_indexed_bodies=missing,sqlite_counts=counts,
    limitation='Identity, type/source information and indexed file presence do not prove every original symbol survived or every decompiled body is semantically correct.')
print(json.dumps(report,indent=2))
