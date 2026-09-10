import frida,json,pathlib,re,sys
mount=re.search(r'weaponAttachment=(0x[0-9a-f]+)',pathlib.Path(sys.argv[2]).read_text())[1]
js=r'''
const b=Process.getModuleByName('PreyDll.dll').base,m=ptr('MOUNT');
const q=p=>Array.from({length:4},(_,i)=>p.add(i*4).readFloat());
const wc=m.add(0x20).readPointer().add(8).readPointer(),s=wc.add(0x10).readPointer();
const h=wc.add(0x38).readPointer(),n=h.sub(4).readU32()&0x7fffffff;
let out={mount:{abs:q(m.add(0x114)),rel:q(m.add(0xf8)),current:q(m.add(0x130)),extra:q(m.add(0x14c))},weapon:wc.toString(),helpers:[]};
for(let i=0;i<Math.min(n,30);i++){
 const a=h.add(i*8).readPointer();if(!a.readPointer().equals(b.add(0x1d212b8)))continue;
 out.helpers.push({name:a.add(0x10).readPointer().readCString(),joint:a.add(0x15c).readS32(),abs:q(a.add(0x114)),rel:q(a.add(0xf8)),current:q(a.add(0x130)),extra:q(a.add(0x14c))});
}send(out);
'''.replace('MOUNT',mount)
s=frida.attach(int(sys.argv[1])); a=s.create_script(js);out=[];a.on('message',lambda m,d:out.append(m));a.load();a.unload();s.detach();pathlib.Path(sys.argv[3]).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
