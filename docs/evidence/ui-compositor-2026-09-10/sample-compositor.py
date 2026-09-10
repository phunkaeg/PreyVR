"""Read CPU/D3D draw state for bounded simulator captures; no GPU readback stall."""
import frida, json, pathlib, subprocess, sys, time
pid, state, prefix = int(sys.argv[1]), sys.argv[2], sys.argv[3]
out = pathlib.Path(sys.argv[4]); count=int(sys.argv[5]) if len(sys.argv)>5 else 12
script=r'''
const b=Process.getModuleByName('PreyDll.dll').base;
const dev=b.add(0x2b3e8e0).readPointer().add(0xaf28).readPointer();
const fn=(p,i,r,a)=>new NativeFunction(p.readPointer().add(i*8).readPointer(),r,a);
const release=p=>{if(p&&!p.isNull())fn(p,2,'uint',['pointer'])(p);};
const cp=Memory.alloc(8);fn(dev,40,'void',['pointer','pointer'])(dev,cp);const ctx=cp.readPointer();
const runtime=p=>{const m=Process.findModuleByAddress(p);return m&&m.name.toLowerCase().includes('xrsim');};
let mapped=null,constants=null;const rows=[];let activeThread=0;const otherThreads={};
Interceptor.attach(ctx.readPointer().add(14*8).readPointer(),{onEnter(a){
 this.watch=a[0].equals(ctx)&&runtime(this.returnAddress)&&a[3].toUInt32()==4;
 if(this.watch){this.out=a[5];this.buffer=a[1];}
},onLeave(r){if(this.watch&&r.toInt32()>=0){mapped={buffer:this.buffer,p:this.out.readPointer()};}}});
Interceptor.attach(ctx.readPointer().add(15*8).readPointer(),{onEnter(a){
 if(mapped&&a[0].equals(ctx)&&a[1].equals(mapped.buffer)){
  constants=[];for(let i=0;i<40;i++)constants.push(mapped.p.add(i*4).readFloat());
  mapped=null;activeThread=Process.getCurrentThreadId();
 }
}});
Interceptor.attach(ctx.readPointer().add(33*8).readPointer(),{onEnter(a){
 if(activeThread&&a[0].equals(ctx)&&activeThread!=Process.getCurrentThreadId()){
  const key=Process.getCurrentThreadId()+':'+this.returnAddress;otherThreads[key]=(otherThreads[key]||0)+1;
 }
}});
function shader(slot){const p=Memory.alloc(8);fn(ctx,slot,'void',['pointer','pointer','pointer','pointer'])(ctx,p,ptr(0),ptr(0));const o=p.readPointer(),s=o.toString();release(o);return s;}
function objectDesc(slot,size){const p=Memory.alloc(8);fn(ctx,slot,'void',['pointer','pointer'])(ctx,p);const o=p.readPointer();if(o.isNull())return null;
 const d=Memory.alloc(size);fn(o,7,'void',['pointer','pointer'])(o,d);const a=[];for(let i=0;i<size/4;i++)a.push(d.add(i*4).readU32());release(o);return a;}
Interceptor.attach(ctx.readPointer().add(13*8).readPointer(),{onEnter(a){
 if(rows.length>=256||!a[0].equals(ctx)||!runtime(this.returnAddress))return;
 const r={thread:Process.getCurrentThreadId(),vertices:a[1].toUInt32(),constants,
  vs:shader(76),ps:shader(74),gs:shader(82),hs:shader(98),ds:shader(102),raster:objectDesc(94,40)};
 const pred=Memory.alloc(8),value=Memory.alloc(4);fn(ctx,86,'void',['pointer','pointer','pointer'])(ctx,pred,value);
 r.predicate=pred.readPointer().toString();r.predicateValue=value.readU32();release(pred.readPointer());
 const p=Memory.alloc(8);fn(ctx,75,'void',['pointer','uint','uint','pointer'])(ctx,0,1,p);let o=p.readPointer();
 if(!o.isNull()){const d=Memory.alloc(52);fn(o,7,'void',['pointer','pointer'])(o,d);r.sampler=[];for(let i=0;i<13;i++)r.sampler.push(d.add(i*4).readU32());release(o);}
 fn(ctx,73,'void',['pointer','uint','uint','pointer'])(ctx,0,1,p);o=p.readPointer();
 if(!o.isNull()){const q=Memory.alloc(8);fn(o,7,'void',['pointer','pointer'])(o,q);const tex=q.readPointer();r.texture=tex.toString();
  const d=Memory.alloc(44);fn(tex,10,'void',['pointer','pointer'])(tex,d);r.textureDesc=[];for(let i=0;i<11;i++)r.textureDesc.push(d.add(i*4).readU32());release(tex);release(o);}
 rows.push(r);
}});
rpc.exports.finish=()=>{Interceptor.detachAll();release(ctx);return {rows,otherThreads};};
'''
session=frida.attach(pid);agent=session.create_script(script);messages=[]
agent.on('message',lambda message,data:messages.append(message));agent.load()
try:
    for i in range(count):
        subprocess.run(['powershell','-NoProfile','-File','D:/Dev Debug/xr-sim/tools/xrsim-cmd.ps1','-Dir',state,'-Lines',f'shot {prefix}-{i:02}','-Quiet'],check=True,stdout=subprocess.DEVNULL)
        time.sleep(.35)
    data=agent.exports_sync.finish();data['messages']=messages
finally:
    agent.unload();session.detach()
out.write_text(json.dumps(data,indent=2))
print(json.dumps({'draws':len(data['rows']),'otherThreads':data['otherThreads'],'messages':messages}))
