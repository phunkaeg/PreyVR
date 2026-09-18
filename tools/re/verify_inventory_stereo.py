"""Execute Steam's matrix arithmetic in Unicorn; never opens a game process.

Requires pefile/unicorn (the local test uses build/re-python). This validates
homogeneous depth and the output layout, not safe movie replay or XR rendering.
"""
import hashlib, json, struct, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build/re-python'))
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
from unicorn.x86_const import *
from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256

def verify():
    blob=DEFAULT_MODULE.read_bytes()
    assert hashlib.sha256(blob).hexdigest()==EXPECTED_SHA256
    pe=pefile.PE(data=blob,fast_load=True)
    image=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
    assert base==0x180000000
    anchors={
        'display':(0x18AB710,'4055564881ec68030000'),
        'calc':(0xDB9620,'488bc455535657488da848ffffff'),
        'world_view':(0xDB968A,'488b81f00100004c8d81f8010000'),
        'zero_projection_offsets':(0xDB96CE,'c745d000000000c745e000000000'),
    }
    for name,(rva,expected) in anchors.items():
        assert image[rva:rva+len(bytes.fromhex(expected))]==bytes.fromhex(expected),name
    assert struct.unpack_from('<Q',image,0x1EB4B60+0x130)[0]==base+0x18AB710
    u=Uc(UC_ARCH_X86,UC_MODE_64)
    u.mem_map(base,(len(image)+4095)&~4095);u.mem_write(base,image)
    arena=0x50000000;stack=0x60000000;stop=0x70000000
    u.mem_map(arena,0x10000);u.mem_map(stack,0x10000);u.mem_map(stop,0x1000)
    def floats(at,values):u.mem_write(at,struct.pack('<'+'f'*len(values),*values))
    ident=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    camera=36882.85546875
    fixtures=[]
    for z in [0,-1000,-15000,15000]:
        u.mem_write(arena,bytes(0x5000))
        u.mem_write(arena+0x1F0,struct.pack('<Q',arena+0x2000))
        u.mem_write(arena+0x27A,b'\1')
        floats(arena+0x98,ident) # native viewport matrix, identity control
        floats(arena+0x1F8,[1,0,0,0,0,-1,0,0,0,0,-1,0,-19200,10800,-camera,1])
        floats(arena+0x238,[2*camera/38400,0,0,0,0,2*camera/21600,0,0,0,0,-1,-1,0,0,-1,0])
        world=ident.copy();world[12:15]=[19200,10800,z]
        floats(arena+0x2000,world)
        floats(arena+0x3000,[1,0,0,0,1,0])
        rsp=stack+0xF008;u.mem_write(rsp,struct.pack('<Q',stop))
        u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RCX,arena)
        u.reg_write(UC_X86_REG_RDX,arena+0x3000);u.reg_write(UC_X86_REG_R8,arena+0x4000)
        u.reg_write(UC_X86_REG_R13,0x123456789ABCD)
        u.emu_start(base+0xDB9620,stop,count=200000)
        assert u.reg_read(UC_X86_REG_RIP)==stop,'did not return'
        assert u.reg_read(UC_X86_REG_R13)==0x123456789ABCD,'callee-saved register control'
        output=list(struct.unpack('<16f',u.mem_read(arena+0x4000,64)))
        assert abs(output[15]-(camera+z))<.01,(z,output)
        assert abs(output[3])<.01 and abs(output[7])<.01,'centred origin control'
        fixtures.append({'world_z':z,'expected_w':camera+z,'matrix':output})
    return {'target':str(DEFAULT_MODULE),'sha256':EXPECTED_SHA256,'image_base':hex(base),
            'method':'Unicorn 2.1.4 execution of unmodified Steam CalcTransMat3D and its matrix multiply callee',
            'anchors':{k:hex(v[0]) for k,v in anchors.items()},'fixtures':fixtures,
            'in_game_tested':False,'claim':'Output is row-major; origin homogeneous W=C+worldZ. Negative native Z is closer.'}

if __name__=='__main__':
    result=verify();text=json.dumps(result,indent=2)
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(text,encoding='utf-8')
    print(text)
