"""Read-only Steam zero-g movement/surface contracts; no game or native DLL load.

EGS/PDB exports supplied names and search leads. These witnesses and the runtime
gate hashes are checked against the supported Steam file, not an EGS RVA delta.
Only derived metadata is emitted. No assets or key material are extracted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from extract_native_hud import TARGET_SHA256, require
from verify_hacking_input import image_reader

BASE=0x180000000
WITNESSES={
    'zero_g_request_type_4':(0x1270EE3,'c745b004000000'),
    'zero_g_request_xyz_stack':(0x1270F89,'8945bc0f14c2f20f1145b4'),
    'zero_g_animated_getter_and_request_call':(0x1270F94,'498b4500ff9098020000488d55b0488bc84c8b0041ff9030010000'),
    'player_animated_field':(0x157C6D0,'488b8108180000c3'),
    'animated_primary_constructor':(0x4F5941,'488d0518827e0148897108488bf948897110488901'),
    'animated_secondary_constructor':(0x4F5956,'488d0573857e0148894140'),
    'movement_type_then_velocity_loads':(0x4F5D09,'833a004c8bf2488bf10f84d7010000f30f104204f30f104a08f30f10520c'),
    'request_type_velocity_copy':(0x4F5E4D,'898678010000f2410f104604f20f11867c010000418b460c898684010000'),
    'last_request_flag_copy':(0x4F5EC0,'410fb646488886c0010000'),
    'physical_owner_entity':(0x4FC088,'4c8b7138'),
    'restriction_flag':(0x159CB0A,'80b91306000000'),
    'entity_parent':(0x904130,'488b81a00000004885c07405488b4018c3c3'),
    'status_component':(0x1587CA0,'488b81c0000000c3'),
    'native_status_owner_component':(0x127008B,'498d8d78060000'),
    'native_status_6_calls':(0x12700BF,'e8dc7b3100488bc8ba06000000e8dff82100'),
    'request_type_4_branch':(0x4FBF10,'c783d406000003000000'),
    'velocity_to_displacement':(0x4FBF5C,'f20f10837c0100008b8384010000f20f1183c40600008983cc060000'),
    'time_step_to_float':(0x4FBF78,'f20f1093d0010000488bcb660f5ad2'),
    'multiply_xyz_by_time':(0x4FBF8D,'f30f5983c4060000f30f598bc8060000f30f5993cc060000'),
    'physical_time_step':(0x4FC0ED,'f20f10bbd0010000'),
    'physical_reciprocal_time':(0x4FC8D6,'f30f101d4a227801f30f5edf'),
    'native_physical_action':(0x4FC9E1,'488d542460488b064533c0488bceff5038'),
    'fixed_collider_constructor_table':(0xB7670D,'488d0574ba2001'),
    'fixed_collider_constructor_class':(0xB76739,'488901488d81a80000004c89512044895128'),
    'fixed_collider_type':(0x1A5A880,'b801000000c3'),
}

def fnv(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value

def lea(read,rva):return rva+7+struct.unpack('<i',read(rva+3,4))[0]

def verify(read,header):
    for name,(rva,value) in WITNESSES.items():
        value=bytes.fromhex(value);require(read(rva,len(value))==value,'witness mismatch: '+name)
    require(lea(read,0x4F5941)==0x1CDDB60,'wrong animated primary receiver')
    require(lea(read,0x4F5956)==0x1CDDED0,'wrong secondary interface witness')
    require(struct.unpack('<Q',read(0x1CDDB60+0x130,8))[0]==BASE+0x4F5D00,'wrong concrete AddMovement callee')
    require(lea(read,0xB7670D)==0x1D82188,'wrong fixed collider receiver')
    require(struct.unpack('<Q',read(0x1D82188+8,8))[0]==BASE+0x1A5A880,'wrong concrete fixed collider type')
    require(struct.unpack('<I',read(0x4FC048+3*4,4))[0]==0x4FBF06,'request type4 not the observed preparation branch')
    # The 1/dt input is a target constant, not a decompiler-inferred unit.
    disp=struct.unpack('<i',read(0x4FC8DA,4))[0]
    require(struct.unpack('<f',read(0x4FC8DE+disp,4))[0]==1.0,'physical conversion numerator not one')
    contracts=re.findall(r'\{(0x[0-9A-Fa-f]+),(\d+),(0x[0-9A-Fa-f]+)ull\}',header)
    require(len(contracts)==15,'runtime contract coverage changed; review explicitly')
    for rva,size,digest in contracts:
        require(fnv(read(int(rva,0),int(size)))==int(digest,0),'runtime gate mismatch at '+rva)
    for name,rva in [('AddMovement',0x4F5D00),('AnimatedVtable',0x1CDDB60),('StaticVtable',0x1D82188),('Caller',0x1270FAF)]:
        found=re.search(r'\b'+name+r'=(0x[0-9A-Fa-f]+)',header)
        require(found is not None and int(found[1],0)==rva,'runtime address mismatch '+name)
    return contracts

def inspect(module,header_path):
    data=module.read_bytes();require(hashlib.sha256(data).hexdigest()==TARGET_SHA256,'unsupported Steam target')
    read=image_reader(data);header=header_path.read_text();contracts=verify(read,header)
    controls=[]
    # Corruption controls operate on this same PE reader; leave the actual file
    # untouched. Each replacement must be reached and rejected by verification.
    for name,address in [('concrete_callee',0x1CDDB60+0x130),('request_type',0x1270EE6),('collider_class',0xB7674A)]:
        reached=[False]
        def corrupt(rva,size):
            out=bytearray(read(rva,size))
            if rva<=address<rva+size:out[address-rva]^=1;reached[0]=True
            return bytes(out)
        try:verify(corrupt,header)
        except ValueError:require(reached[0],'negative control never reached '+name)
        else:raise ValueError('corruption accepted: '+name)
        controls.append(name+'_rejected')
    bad=header.replace('0xD4985D11A1DE1655','0xD4985D11A1DE1654');require(bad!=header,'hash control not applied')
    try:verify(read,bad)
    except ValueError:controls.append('runtime_hash_mismatch_rejected')
    else:raise ValueError('bad runtime hash accepted')
    return dict(target_sha256=TARGET_SHA256,image_base=hex(BASE),environment='static; local Steam file and EGS export leads',
        witnesses={n:dict(rva=hex(a),bytes=b) for n,(a,b) in WITNESSES.items()},
        runtime_gates=[dict(rva=a,size=int(b),fnv64=c) for a,b,c in contracts],negative_controls=controls,
        contract=dict(native_request='type4; RCX primary animated receiver; RDX caller-owned request; velocity12 bytes at+4',
            caller_return='0x1270faf',animated_primary='0x1cddb60',add_movement='0x4f5d00',
            velocity_units='world displacement/time: preparation multiplies by dt, physics action divides by dt',
            fixed_collider='primary vtable0x1d82188; concrete GetType()=1; constructor-zeroed word+0x28=0 as an additional conservative gate',
            retained_pointer='token only; fresh short scene query revalidates the held anchor'),
        limits='No game/native physics acceptance, XR session, surface coverage, headset feel or performance validation. Native AddMovement return is submission only. Fixed geometry first phase; moving supports and two-hand anchors excluded.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module',type=Path,default=Path('D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll'))
    parser.add_argument('--header',type=Path,default=Path(__file__).resolve().parents[2]/'include/preyvr/SurfaceGrabNative.h')
    parser.add_argument('--out',type=Path)
    args=parser.parse_args();report=inspect(args.module,args.header);text=json.dumps(report,indent=2)+'\n'
    if args.out:args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(text,encoding='utf-8')
    print(text)
