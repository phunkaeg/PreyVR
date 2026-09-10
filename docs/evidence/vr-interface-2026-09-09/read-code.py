import frida,json,pathlib,sys
s=frida.attach(int(sys.argv[1])); out=[]
js=r'''
const b=Process.getModuleByName('PreyDll.dll').base;
let p=b.add(0x27ebd0),out=[];
for(let i=0;i<50;i++){const x=Instruction.parse(p);out.push({rva:p.sub(b).toString(),asm:x.toString()});p=x.next;}
send(out);
'''
a=s.create_script(js);a.on('message',lambda m,d:out.append(m));a.load();a.unload();s.detach();pathlib.Path(sys.argv[2]).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
