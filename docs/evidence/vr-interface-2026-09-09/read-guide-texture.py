"""One readback on xr-sim's own draw thread; never polls the immediate context."""
import frida, json, pathlib, subprocess, sys, time
pid, state, out = int(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
script = r'''
const b=Process.getModuleByName('PreyDll.dll').base;
const dev=b.add(0x2b3e8e0).readPointer().add(0xaf28).readPointer();
const fn=(p,slot,result,args)=>new NativeFunction(p.readPointer().add(slot*8).readPointer(),result,args);
const release=p=>{if(p&&!p.isNull())fn(p,2,'uint',['pointer'])(p);};
const cp=Memory.alloc(8);fn(dev,40,'void',['pointer','pointer'])(dev,cp);const ctx=cp.readPointer();
let pending=null,done=false;
Interceptor.attach(ctx.readPointer().add(8*8).readPointer(),{onEnter(a){
 if(done||pending||a[1].toUInt32()!=0||a[2].toUInt32()!=1)return;
 const m=Process.findModuleByAddress(this.returnAddress);if(!m||!m.name.toLowerCase().includes('xrsim'))return;
 const srv=a[3].readPointer();if(srv.isNull())return;
 const rp=Memory.alloc(8);fn(srv,7,'void',['pointer','pointer'])(srv,rp);const tex=rp.readPointer();
 const desc=Memory.alloc(44);fn(tex,10,'void',['pointer','pointer'])(tex,desc);
 if(desc.readU32()!=1024||desc.add(4).readU32()!=160){release(tex);return;}
 pending={tex,desc,thread:Process.getCurrentThreadId()};
}});
Interceptor.attach(ctx.readPointer().add(13*8).readPointer(),{onLeave(){
 if(!pending||done||pending.thread!=Process.getCurrentThreadId())return;
 const p=pending;pending=null;done=true;let staging=null;
 try {
  const format=p.desc.add(16).readU32();
  p.desc.add(28).writeU32(3);p.desc.add(32).writeU32(0);p.desc.add(36).writeU32(0x20000);p.desc.add(40).writeU32(0);
  const sp=Memory.alloc(8);const hr=fn(dev,5,'int',['pointer','pointer','pointer','pointer'])(dev,p.desc,ptr(0),sp);
  if(hr<0)throw new Error('CreateTexture2D '+hr);staging=sp.readPointer();
  fn(ctx,47,'void',['pointer','pointer','pointer'])(ctx,staging,p.tex);
  const mapped=Memory.alloc(16);const mapHr=fn(ctx,14,'int',['pointer','pointer','uint','uint','uint','pointer'])(ctx,staging,0,1,0,mapped);
  if(mapHr<0)throw new Error('Map '+mapHr);
  const pitch=mapped.add(8).readU32();
  send({kind:'texture',width:1024,height:160,format,rowPitch:pitch,thread:Process.getCurrentThreadId()},mapped.readPointer().readByteArray(pitch*160));
  fn(ctx,15,'void',['pointer','pointer','uint'])(ctx,staging,0);
 }catch(e){send({kind:'error',error:String(e)});}finally{release(staging);release(p.tex);}
}});
rpc.exports.finish=()=>{Interceptor.detachAll();if(pending)release(pending.tex);release(ctx);return {captured:done};};
'''
messages=[]
def receive(message,data):
    messages.append(message)
    if data:out.with_suffix('.bgra').write_bytes(data)
session=frida.attach(pid);agent=session.create_script(script);agent.on('message',receive);agent.load()
try:
    subprocess.run(['powershell','-NoProfile','-File','D:/Dev Debug/xr-sim/tools/xrsim-cmd.ps1','-Dir',state,'-Lines','shot guide-probe-07','-Quiet'],check=True,stdout=subprocess.DEVNULL)
    time.sleep(2)
    messages.append(agent.exports_sync.finish())
finally:
    agent.unload();session.detach()
out.write_text(json.dumps(messages,indent=2));print(json.dumps(messages,indent=2))
