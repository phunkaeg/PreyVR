"""Bounded downstream observer; never hooks the guarded mouse entry prologue."""
import json
import sys
import time
import subprocess
from pathlib import Path
import frida

pid, duration, output = int(sys.argv[1]), float(sys.argv[2]), Path(sys.argv[3])
events = []
session = frida.attach(pid)
script = session.create_script(r'''
const base=Process.getModuleByName('PreyDll.dll').base;
let count=0;
const seenRoots=new Set();
for(const rva of [0x2cef30,0x2ff0c0,0xe8cb20]) {
  Interceptor.attach(base.add(rva),{onEnter(a){
    if(count++>=600)return;
    const m=Process.findModuleByAddress(this.returnAddress);
    const row={rva:rva.toString(16),ms:Date.now(),thread:Process.getCurrentThreadId(),
      from:m?m.name+'+'+this.returnAddress.sub(m.base):this.returnAddress.toString()};
    try {
      row.event=rva===0xe8cb20?[0,4,8,12,16].map(o=>a[1].add(o).readS32()):
        [a[1].toInt32(),a[2].toInt32(),a[3].toInt32(),a[4].toInt32()];
      if(rva===0xe8cb20){
        const root=a[0].add(0xb0).readPointer();
        seenRoots.add(root.toString());
        const vt=root.readPointer();
        row.root=root.toString();row.rootVtable=vt.sub(base).toString();
        row.handleEvent=vt.add(0x168).readPointer().sub(base).toString();
      }
    }catch(e){row.error=String(e);}
    send(row);
  }});
}
Interceptor.attach(base.add(0x18af100),{
  onEnter(a){
    if(!seenRoots.has(a[0].toString()))return;
    this.root=a[0];this.row={processMouse:true,ms:Date.now()};
    try {
      this.row.xy=[a[2].add(4).readFloat(),a[2].add(8).readFloat()];
      this.row.buttons=a[2].add(12).readU16();this.row.flags=a[2].add(15).readU8();
      this.row.before=[a[0].add(0x9d0).readFloat(),a[0].add(0x9d4).readFloat(),a[0].add(0x9c8).readU32()];
    }catch(e){this.row.error=String(e);}
  },
  onLeave(){if(this.row){try{this.row.after=[this.root.add(0x9d0).readFloat(),this.root.add(0x9d4).readFloat(),this.root.add(0x9c8).readU32()];}catch(e){}send(this.row);}}
});
// Candidate 07 owns OnPick's prologue. Observe downstream drag/place only.
for(const [rva,name] of [[0x162cb70,'drag'],[0x162d330,'place']]) {
  Interceptor.attach(base.add(rva),{
    onEnter(a){
      this.self=a[0];this.row={inventory:name,ms:Date.now(),args:[]};
      try {
        const p=a[3].readPointer(),n=Math.min(p.sub(4).readU32()&0x7fffffff,8);
        for(let i=0;i<n;i++){const v=p.add(i*0x20),type=v.add(8).readS32();let text;if(type===4){try{text=v.add(0x10).readPointer().readUtf8String();}catch(e){text=String(e);}}this.row.args.push({type,bits:v.add(0x10).readU32(),f32:v.add(0x10).readFloat(),text});}
        this.row.before=[0x20,0x24,0x28,0x34].map(o=>a[0].add(o).readU32());
      }catch(e){this.row.error=String(e);}
    },
    onLeave(){try{this.row.after=[0x20,0x24,0x28,0x34].map(o=>this.self.add(o).readU32());}catch(e){}send(this.row);}
  });
}
send({ready:true,base:base.toString()});
''')
script.on('message', lambda message, data: events.append(message))
try:
    script.load()
    print('observer ready', flush=True)
    if len(sys.argv) > 4:
        steps = json.loads(Path(sys.argv[4]).read_text(encoding='utf-8'))
        for step in steps:
            command = step['command']
            if command.startswith('shot '): command += '-'+output.stem
            events.append({'control': command, 'ms': round(time.time()*1000)})
            subprocess.run(['powershell.exe', '-NoProfile', '-File',
                'D:/Dev Debug/xr-sim/tools/xrsim-cmd.ps1', '-Dir',
                sys.argv[5] if len(sys.argv)>5 else 'D:/Dev Debug/PreyVR/docs/evidence/ui-pointer-2026-09-10/state-d',
                '-Quiet', '-Lines', command], check=True, stdout=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW)
            print(command, flush=True)
            time.sleep(step.get('wait', 1))
    time.sleep(duration)
finally:
    script.unload()
    session.detach()
    output.write_text(json.dumps(events, indent=2), encoding='utf-8')
    print(f'{len(events)} records: {output}', flush=True)
