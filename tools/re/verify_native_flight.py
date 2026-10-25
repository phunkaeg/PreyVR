"""Read-only Steam native flight contracts; emits derived facts, not game assets.

Requires the existing PAK reader dependencies via --deps. No native DLL load,
process attachment, runtime change or game launch. All addresses are RVAs.
"""
import argparse,hashlib,json,re,struct,sys
from pathlib import Path
from extract_native_hud import Pak,TARGET_SHA256,KEY_FILE_OFFSET,KEY_BYTES,cipher_self_test,decode_xml,read_at,require,sha256_file
from verify_hacking_input import image_reader

WITNESSES={
 'entity_constructor_vtable':(0x9018C2,'488d0507f34201488bf9488901'),
 'native_fsm_dispatch':(0x159EA28,'8b03ffc883f810'),
 'native_fsm_case9':(0x159EAF8,'488d8b100300000f28d6488bd3e8b611cdff'),
 'zero_g_entry_state':(0x159C45F,'c70609000000'),
 'zero_g_receiver_entity':(0x126FDD0,'4d8b7d38'),
 'zero_g_thrust_rotation_read':(0x1270640,'498b07488d542458498bcfff9050010000'),
 'rotation_hidden_result':(0x904D4B,'488bda'),
 'rotation_wxyz_result_tail':(0x905153,'f30f11730c'),
 'rotation_result_return':(0x90515D,'488bc3'),
 'input_component_from_player':(0x159CBB1,'488d88e0080000488bd3e8b027ffff'),
 'input_owner_from_receiver':(0x158F380,'488b4970'),
 'jump_receiver_to_fsm':(0x158FEEF,'488b5b70400fb6d7488d4b58e8c0e60000'),
 'jump_setter':(0x159E5C0,'889111060000'),
 'jump_consumer':(0x159D410,'0fb68111060000'),
 'sneak_setter':(0x159E5D0,'889110060000'),
 'sneak_consumer':(0x159D420,'0fb68110060000'),
 'zero_g_ascent_request':(0x1270480,'498bcce888cf3200'),
 'zero_g_descent_request':(0x12704E2,'498bcce836cf3200'),
 'sprint_native_setter':(0x1591124,'8891ac00000084d27432'),
 'sprint_native_consumer':(0x127024D,'4138bd8c090000'),
 'lean_cinematic_clear':(0x15910B0,'83b99400000000740966c781b50000000000'),
 'lean_left_flag':(0x15910C2,'0fb691b5000000'),
 'lean_right_flag':(0x15910DD,'0fb681b6000000'),
 'lean_sum':(0x15910F0,'f30f58c1f30f118180000000f30f11417c'),
 'brake_both_flags':(0x1591101,'84d2740c84c07408c681b100000001c3'),
 'brake_native_consumer':(0x1270366,'4138b591090000'),
}
RUNTIME_ARRAYS={'getter':0x904D30,'site':0x1270640,'state':0x159C45F,'sprint':0x1591124,
 'inputBase':0x159CBB1,'leanLeft':0x15910C2,'leanRight':0x15910DD,'braking':0x1591109,'getPlayer':0x157C990}

def verify(data,source):
 read=image_reader(data)
 for name,(rva,hexdata) in WITNESSES.items():
  expected=bytes.fromhex(hexdata);require(read(rva,len(expected))==expected,'native witness mismatch: '+name)
 require(struct.unpack('<I',read(0x159EBE8+8*4,4))[0]==0x159EAF8,'state 9 dispatch mismatch')
 displacement=struct.unpack('<i',read(0x9018C5,4))[0]
 require(0x9018C9+displacement==0x1D30BD0,'constructor receiver vtable mismatch')
 require(struct.unpack('<Q',read(0x1D30BD0+0x150,8))[0]==0x180904D30,'concrete vtable slot mismatch')
 for name,rva in RUNTIME_ARRAYS.items():
  match=re.search(r'const unsigned char '+name+r'\[\]=\{([^}]+)\}',source)
  require(match is not None,'missing runtime gate '+name)
  expected=bytes(int(x.strip(),0) for x in match[1].split(','))
  require(read(rva,len(expected))==expected,'runtime prefix mismatch: '+name)
 # Positive/negative vertical controls are numerical target facts, not enum guesses.
 for instruction,disp_at,size,expected in [(0x12701BD,5,9,1.0),(0x1270500,5,9,-1.0)]:
  disp=struct.unpack('<i',read(instruction+disp_at,4))[0]
  value=struct.unpack('<f',read(instruction+size+disp,4))[0]
  require(value==expected,'vertical thrust sign mismatch')
 return read

def inspect(game,source_path):
 from Crypto.PublicKey import RSA
 module=game/'Binaries/Danielle/x64/Release/PreyDll.dll';data=module.read_bytes()
 require(hashlib.sha256(data).hexdigest()==TARGET_SHA256,'unsupported Steam target')
 source=source_path.read_text();read=verify(data,source)
 # Mutate the actual vtable witness, not just the file identity gate.
 wrong=bytearray(data);needle=struct.pack('<Q',0x180904D30);at=wrong.find(needle)
 require(at>=0 and wrong.find(needle,at+1)<0,'unexpected getter pointer coverage')
 wrong[at]^=1
 try:verify(wrong,source)
 except ValueError:pass
 else:raise ValueError('corrupted concrete callee accepted')
 wrong_source=source.replace('0x0f,0xb6,0x91,0xb5','0x0f,0xb6,0x91,0xb4')
 require(wrong_source!=source,'runtime-gate negative control not applied')
 try:verify(data,wrong_source)
 except ValueError:pass
 else:raise ValueError('wrong receiver-field runtime gate accepted')
 cipher_self_test()
 with module.open('rb') as stream:key=RSA.import_key(read_at(stream,KEY_FILE_OFFSET,KEY_BYTES))
 profiles=[]
 expected={'jump':'xi_a','sneak':'xi_b','sprint':'xi_thumbl','leanleft':'xi_shoulderl','leanright':'xi_shoulderr'}
 for relative in ('GameSDK/GameData.pak','GameSDK/Precache/patch.pak'):
  pak=Pak(game/relative,key);raw=pak.read('libs/config/defaultprofile.xml');root=decode_xml(raw)
  maps=[m for m in root.iter('actionmap') if m.get('name')=='player'];require(len(maps)==1,'ambiguous player map')
  rows=[]
  for name,pad in expected.items():
   actions=[a for a in maps[0].findall('action') if a.get('name')==name];require(len(actions)==1,'missing/duplicate action: '+name)
   a=actions[0];require(a.get('xboxpad')==pad and a.get('onPress')=='1' and a.get('onRelease')=='1','binding mismatch: '+name)
   rows.append(dict(action=name,xboxpad=pad,onPress=True,onRelease=True,onHold=a.get('onHold')=='1'))
  profiles.append(dict(archive=relative,archive_sha256=sha256_file(game/relative),member_sha256=hashlib.sha256(raw).hexdigest(),bindings=rows))
 return dict(target_sha256=TARGET_SHA256,image_base='0x180000000',environment='static',
  witnesses={n:dict(rva=hex(a),bytes=b) for n,(a,b) in WITNESSES.items()},profiles=profiles,
  controls=dict(corrupt_callee_rejected=True,corrupt_runtime_field_gate_rejected=True,twofish_known_answer='PASS'),
  contract=dict(state=9,fsm_offset='0x58',input_offset='0x8e0',entity_offset='0x38',
   thrust_getter_return='0x1270651',entity_vtable='0x1d30bd0',getter='0x904d30',vtable_byte_slot='0x150',
   native_quaternion='w,x,y,z; caller supplies 16-byte return buffer in RDX',
   brake='paired input flags +0xB5/+0xB6 then ProcessLeanInput at 0x15910B0; sum zero, braking true',
   boost='ProcessSprintInput at 0x1591120; input +0xAC, false clears independently of gamepad onPress latch'),
  limits='Static target/default bindings and source gate correspondence only. No game, native input acceptance, headset comfort or performance test. EGS symbols supplied search leads; shipping RVAs and layout are Steam byte observations.')

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--game',type=Path,default=Path('D:/SteamLibrary/steamapps/common/Prey'))
 parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/dll/FlightNative.cpp')
 parser.add_argument('--deps',type=Path);parser.add_argument('--out',type=Path)
 args=parser.parse_args()
 if args.deps:sys.path.insert(0,str(args.deps.resolve()))
 report=inspect(args.game,args.source);text=json.dumps(report,indent=2)+'\n'
 if args.out:args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(text,encoding='utf-8')
 print(text)
