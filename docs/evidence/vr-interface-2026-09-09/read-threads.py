import frida,json,pathlib,sys
s=frida.attach(int(sys.argv[1]));out=[]
a=s.create_script(r'''
let ts=[];
for(const t of Process.enumerateThreads()) {
 const frames=Thread.backtrace(t.context,Backtracer.ACCURATE).slice(0,12).map(p=>{const m=Process.findModuleByAddress(p);return m?m.name+'+'+p.sub(m.base):p.toString();});
 ts.push({id:t.id,state:t.state,frames});
}
send({threads:ts});
''');a.on('message',lambda m,d:out.append(m));a.load();a.unload();s.detach()
pathlib.Path(sys.argv[2]).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
