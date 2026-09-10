import frida, json, pathlib, subprocess, sys, time
pid, state, output = int(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
js = r'''
const b=Process.getModuleByName('PreyDll.dll').base;
const q=p=>Array.from({length:4},(_,i)=>p.add(i*4).readFloat());
function read(w) {
 send({weapon:w.toString(),vtable:w.readPointer().sub(b).toString(),mount:w.add(0x2b0).readPointer().toString(),helperNamePointer:w.add(0x2f0).readPointer().toString()});
 const mount=w.add(0x2b0).readPointer(),binding=mount.add(0x20).readPointer();
 const wc=binding.add(8).readPointer(),manager=wc.add(0x18),s=wc.add(0x10).readPointer();
 const helpers=manager.add(0x20).readPointer(), joints=s.add(8).readPointer();
 const name=w.add(0x2f0).readPointer();
 let o={weapon:w.toString(),vtable:w.readPointer().sub(b).toString(),mount:mount.toString(),weaponCharacter:wc.toString(),managerVtable:manager.readPointer().sub(b).toString(),managerOwner:manager.add(0x18).readPointer().toString(),helperNamePointer:name.toString(),helperName:Process.findRangeByAddress(name)?name.readCString():null,helpers:helpers.toString(),helperCount:helpers.isNull()?0:helpers.sub(4).readU32(),jointCount:joints.sub(4).readU32(),names:[],attachments:[]};
 send({...o,stage:'header'});
 for(let i=0;i<Math.min(o.jointCount&0x7fffffff,100);i++){try{const p=joints.add(i*0xa8);o.names.push({index:i,name:p.readPointer().readCString(),parent:p.add(0x18).readS16(),bind:q(s.add(0x30).readPointer().add(i*28))});}catch(e){o.names.push({index:i,error:String(e)});}}
 for(let i=0;i<Math.min(o.helperCount&0x7fffffff,30);i++){try{const h=helpers.add(i*8).readPointer();o.attachments.push({name:h.add(0x10).readPointer().readCString(),vtable:h.readPointer().sub(b).toString(),joint:h.add(0x15c).readS32(),abs:q(h.add(0x114))});}catch(e){o.attachments.push({index:i,error:String(e)});}}
 send(o);
}
Interceptor.attach(b.add(0x16914f0),{onEnter(a){this.w=a[0];},onLeave(r){try{if(r.toInt32())read(this.w);}catch(e){send({error:String(e)});}}});
if('DIRECT'!='DIRECT_PLACEHOLDER')try{read(ptr('DIRECT'));}catch(e){send({error:String(e)});}
'''
js=js.replace('DIRECT_PLACEHOLDER','UNUSED').replace('DIRECT',sys.argv[5] if len(sys.argv)>5 else 'UNUSED')
s=frida.attach(pid); a=s.create_script(js); out=[]
a.on('message',lambda m,d:out.append(m));a.load()
subprocess.run(['powershell','-NoProfile','-File','D:/Dev Debug/xr-sim/tools/xrsim-cmd.ps1','-Dir',state,'-Lines',sys.argv[4],'-Quiet'],check=True,capture_output=True)
time.sleep(3)
a.unload();s.detach();output.write_text(json.dumps(out,indent=2));print(output.read_text())
