import frida,json,pathlib,re,sys,time
log=pathlib.Path(sys.argv[2]).read_text()
mount=re.search(r'weaponAttachment=(0x[0-9a-f]+)',log)[1]
js=r'''
const b=Process.getModuleByName('PreyDll.dll').base;
const m=ptr('MOUNT');const c=m.add(0x28).readPointer().add(0x18).readPointer(),s=c.add(0x10).readPointer();
const readQ=p=>Array.from({length:4},(_,i)=>p.add(i*4).readFloat());
let out={mount:m.toString(),character:c.toString(),skeleton:s.toString(),joint:m.add(0x15c).readS32(),flags:m.add(8).readU32(),abs:readQ(m.add(0x114)),rel:readQ(m.add(0xf8)),extra:readQ(m.add(0x14c)),fields:[]};
for(let i=0x18;i<0x58;i+=8){const p=s.add(i).readPointer();let row={offset:i,p:p.toString()};try{row.prefix=p.sub(8).readByteArray(40);row.q=readQ(p.add(out.joint*28));row.count=p.sub(4).readU32();}catch(e){row.error=String(e);}out.fields.push(row);}
const vt=s.add(0x18).readPointer(), fn=vt.add(0x48).readPointer();
out.poseVtable=vt.sub(b).toString();out.absGetter=fn.sub(b).toString();out.getterBytes=Array.from(new Uint8Array(fn.readByteArray(32)));out.poseCount=s.add(0x20).readU32();send(out);
let done=false;Interceptor.attach(b.add(0x877b50),{onEnter(a){if(done || !a[0].equals(c))return;done=true;
 const pose=a[1].add(8).readPointer(),abs=pose.add(0x18).readPointer();
 send({abs:abs.toString(),prefix:Array.from(new Uint8Array(abs.sub(16).readByteArray(16))),count:abs.sub(4).readU32(),wrist45:readQ(abs.add(45*28)),socket:readQ(abs.add(out.joint*28)),bind:readQ(s.add(0x30).readPointer().add(out.joint*28))});}});
'''.replace('MOUNT',mount)
session=frida.attach(int(sys.argv[1]));a=session.create_script(js);out=[]
a.on('message',lambda m,d:out.append(m));a.load();time.sleep(.2);a.unload();session.detach()
p=pathlib.Path(sys.argv[3]);p.write_text(json.dumps(out,indent=2));print(p.read_text())
