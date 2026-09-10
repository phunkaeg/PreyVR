"""Bounded observers only; no native calls, replacement returns or UI writes."""
import hashlib, json, sys, time
from pathlib import Path
import frida

pid=int(sys.argv[1]); output=Path(sys.argv[2])
source=Path('D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll')
assert hashlib.sha256(source.read_bytes()).hexdigest()=='7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'
js=r'''
const b=Process.getModuleByName('PreyDll.dll').base;
const result={pid:Process.id,base:b.toString(),started:Date.now(),lookup:0,display:0,
 worldCalls:0,nullWorld:0,viewCalls:0,matrices:[],views:[],errors:[],identity:[]};
const roots=new Set(),active=new Map(),unique=new Set();
function fail(e){if(result.errors.length<8)result.errors.push(String(e));}
function mat(p){return Array.from({length:16},(_,i)=>p.add(i*4).readFloat());}
Interceptor.attach(b.add(0x2ce410),{
 onEnter(a){this.pda=false;try{this.pda=a[1].readCString()==='DaniellePDA';}catch(e){}},
 onLeave(p){if(!this.pda||p.isNull())return;try{
  if(!p.readPointer().equals(b.add(0x1cab358)))return;
  const player=p.add(0x58).readPointer();if(player.isNull()||!player.readPointer().equals(b.add(0x1db56d8)))return;
  const root=player.add(0xb0).readPointer();if(root.isNull()||!root.readPointer().equals(b.add(0x1eb4b60)))return;
  result.lookup++;if(!roots.has(root.toString()))result.identity.push({element:p.toString(),player:player.toString(),root:root.toString(),visible:p.add(0x70).readU8()});
  roots.add(root.toString());
 }catch(e){fail(e);}}
});
Interceptor.attach(b.add(0x18ab710),{
 onEnter(a){this.tid=Process.getCurrentThreadId();this.old=active.get(this.tid);this.ours=roots.has(a[0].toString());active.set(this.tid,this.ours);if(this.ours)result.display++;},
 onLeave(){if(this.old===undefined)active.delete(this.tid);else active.set(this.tid,this.old);}
});
Interceptor.attach(b.add(0xdbe300),{onEnter(a){if(!active.get(Process.getCurrentThreadId()))return;try{
 result.worldCalls++;if(a[1].isNull()){result.nullWorld++;return;}
 if(result.display%60!==1)return;
 const m=mat(a[1]),key=result.display+':'+m.map(x=>x.toFixed(4)).join(',');
 if(!unique.has(key)&&result.matrices.length<5000){unique.add(key);result.matrices.push({ms:Date.now(),display:result.display,m});}
}catch(e){fail(e);}}});
Interceptor.attach(b.add(0xdbe1e0),{onEnter(a){if(!active.get(Process.getCurrentThreadId()))return;try{
 result.viewCalls++;if(result.views.length<8)result.views.push(mat(a[1]));
}catch(e){fail(e);}}});
rpc.exports.finish=()=>{Interceptor.detachAll();result.ended=Date.now();return result;};
'''
session=None;agent=None
try:
 session=frida.attach(pid);agent=session.create_script(js);agent.load()
 print('Observer attached for 4 seconds',flush=True)
 time.sleep(4)
 data=agent.exports_sync.finish();output.write_text(json.dumps(data,indent=2),encoding='utf-8')
 print(json.dumps({k:v for k,v in data.items() if k not in ('matrices','views')},indent=2));print('matrix samples',len(data['matrices']))
finally:
 if agent:
  try:agent.unload()
  except Exception:pass
 if session:
  try:session.detach()
  except Exception:pass
