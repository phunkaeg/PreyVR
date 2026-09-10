import frida, json, pathlib, time, sys
pid = int(sys.argv[1])
script = r'''
const b=Process.getModuleByName('PreyDll.dll').base;
const names={}, entries={}, callbacks={};
const dev=b.add(0x2b3e8e0).readPointer().add(0xaf28).readPointer();
const cp=Memory.alloc(8);new NativeFunction(dev.readPointer().add(40*8).readPointer(),'void',['pointer','pointer'])(dev,cp);
const ctx=cp.readPointer();
const getRt=new NativeFunction(ctx.readPointer().add(89*8).readPointer(),'void',['pointer','uint','pointer','pointer']);
const release=p=>{if(!p.isNull())new NativeFunction(p.readPointer().add(16).readPointer(),'uint',['pointer'])(p);};
const active={}, bindings={};
Interceptor.attach(ctx.readPointer().add(33*8).readPointer(),{onEnter(a){
 const tid=Process.getCurrentThreadId();if(!active[tid])return;
 const targets=[];for(let i=0;i<a[1].toUInt32();i++)targets.push(a[2].add(i*8).readPointer().toString());
 const key=active[tid]+':'+targets.join(',')+':'+a[3];tick(bindings,key,{name:active[tid],targets,dsv:a[3].toString()});
}});
function tick(map,key,row) { if(!map[key])map[key]={...row,count:0};map[key].count++; }
Interceptor.attach(b.add(0x2febc0),{onEnter(a){
 try {const e=a[0],p=e.add(0x58).readPointer();const name=e.add(0x30).readPointer().readCString();
 names[p.add(8).toString()]=name;
 tick(entries,name,{element:e.toString(),player:p.toString(),visible:e.add(0x70).readU8(),thread:Process.getCurrentThreadId()});
 }catch(e) {send({error:String(e)});}
}});
for(const rva of [0xe8c5e0,0xe8c770]) Interceptor.attach(b.add(rva),{onEnter(a){
 const key=a[0].toString()+'-'+rva;
 this.tid=Process.getCurrentThreadId();active[this.tid]=names[a[0].toString()]||a[0].toString();
 const rt=Memory.alloc(8),ds=Memory.alloc(8);getRt(ctx,1,rt,ds);
 tick(callbacks,key,{proxy:a[0].toString(),rva,thread:this.tid,rt:rt.readPointer().toString(),ds:ds.readPointer().toString()});
 release(rt.readPointer());release(ds.readPointer());
},onLeave(){delete active[this.tid];}});
rpc.exports.finish=function(){Interceptor.detachAll();release(ctx);return {entries,callbacks,names,bindings};};
'''
session=frida.attach(pid)
agent=session.create_script(script)
agent.load()
time.sleep(.35)
data=agent.exports_sync.finish()
agent.unload();session.detach()
path=pathlib.Path(sys.argv[2]);path.write_text(json.dumps(data,indent=2))
print(json.dumps(data,indent=2))
