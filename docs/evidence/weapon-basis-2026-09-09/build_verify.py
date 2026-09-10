"""Offline build and tests. Does not start or attach to Prey."""
from pathlib import Path
import os, subprocess, sys

out = Path(__file__).resolve().parent
root = out.parents[2]
commands = [
    ('build.log', ['C:/Program Files/CMake/bin/cmake.exe', '--build', 'build/headless', '--config', 'Release', '--parallel', '6']),
    ('alignment-tests.log', ['build/headless/Release/preyvr_weapon_rig_alignment_tests.exe']),
    ('ctest.log', ['C:/Program Files/CMake/bin/ctest.exe', '--test-dir', 'build/headless', '-C', 'Release', '--output-on-failure']),
]
for name, cmd in commands:
    with (out/name).open('w', encoding='utf-8') as stream:
        stream.write('Command: '+repr(cmd)+'\n'); stream.flush()
        result = subprocess.run(cmd, cwd=root, env=dict(os.environ), stdout=stream, stderr=subprocess.STDOUT)
    print(name, 'exit', result.returncode, flush=True)
    if result.returncode:
        print((out/name).read_text()[-6000:]); sys.exit(result.returncode)
print((out/'ctest.log').read_text()[-550:])
