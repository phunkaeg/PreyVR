from pathlib import Path
import subprocess,os,sys
out=Path('docs/evidence/build-takeover-2026-09-09')
for name,cmd in [('build.log',[r'C:/Program Files/CMake/bin/cmake.exe','--build','build/headless','--config','Release','--parallel','6']),('ctest.log',[r'C:/Program Files/CMake/bin/ctest.exe','--test-dir','build/headless','-C','Release','--output-on-failure'])]:
    with (out/name).open('w',encoding='utf8') as f:
        f.write('Command: '+repr(cmd)+'\n'); f.flush()
        r=subprocess.run(cmd,env=dict(os.environ),stdout=f,stderr=subprocess.STDOUT)
    print(name,'exit',r.returncode,flush=True)
    if r.returncode:
        print((out/name).read_text()[-5000:]);sys.exit(r.returncode)
print((out/'ctest.log').read_text()[-900:])
