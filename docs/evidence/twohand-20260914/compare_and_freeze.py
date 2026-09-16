import json, shutil, hashlib, math, re
from pathlib import Path

root=Path.cwd()
out=root/'docs/evidence/twohand-20260914'
out.mkdir(parents=True,exist_ok=True)
run=root/'build/twohand-live/runs/run-20260914-075011'
files={
 'run.json':run/'run.json', 'PreyVR.log':run/'log/PreyVR.log',
 'xrsim.log':run/'xrsim/xrsim.log', 'xrsim-final.json':run/'xrsim/state.json',
 'PreyVR-tested.dll':root/'build/inventory-stereo/Release/PreyVR.dll',
 'build.log':root/'build/twohand-validation/build.log',
 'tests.log':root/'build/twohand-validation/tests.log',
 'lifecycle.txt':root/'build/twohand-live/lifecycle.txt',
}
for name in ['report-held-positive.txt','report-steered.txt','report-released.txt','report-final.txt']:
 files[name]=root/'build/twohand-live'/name
for tag in ['twohand_positive','twohand_steered','twohand_held_v2','twohand_second_weapon']:
 for suffix in ['.json','_left.png','_right.png']:
  files[tag+suffix]=run/'xrsim/capture'/(tag+suffix)
for src in ['src/common/TwoHandedAim.cpp','include/preyvr/TwoHandedAim.h','tests/TwoHandedAimTests.cpp',
            'src/dll/AimTakeover.cpp','src/dll/AimTakeover.h','src/dll/AnimIkTakeover.cpp','src/dll/XrInput.cpp','src/dll/MoveLane.cpp']:
 files[src.replace('/','__')]=root/src
for name,src in files.items(): shutil.copy2(src,out/name)

def field(name,key):
 text=(out/name).read_text(encoding='utf-8-sig')
 return [float(v) for v in re.search(r'\b'+key+r'=(\S+)',text)[1].split(',')]
primary=[field(n,'rpRawQ') for n in ['report-held-positive.txt','report-steered.txt','report-released.txt']]
directions=[field(n,'rpDir') for n in ['report-held-positive.txt','report-steered.txt','report-released.txt']]
def angle(a,b): return math.degrees(math.acos(max(-1,min(1,sum(x*y for x,y in zip(a,b))/math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))))))
result={'primary_orientations':primary,'directions':directions,
        'steer_degrees':angle(directions[0],directions[1]),'release_error_degrees':angle(directions[0],directions[2])}
assert primary[0]==primary[1]==primary[2]==[0,0,0,1]
assert result['steer_degrees']>15 and result['release_error_degrees']<.2
(out/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
shutil.copy2(__file__,out/'compare_and_freeze.py')

receipt=root/'receipts/20260914-twohand-native-grip.json'
r=json.loads(receipt.read_text())
r.update(target={'name':'Steam PreyDll.dll with PreyVR BFCDBB272FFF37663233F5631D1741EEE386FAFF2CF01584E74C6E8D47FA01E4',
 'identity_kind':'sha256','identity':'7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'},
 question='Can the existing native wrist/barrel alignment supply a support region and drive coherent two-handed GLOO aim?',
 claim='The native GLOO wrists provide a repeatable support region. A fresh squeeze delivered through the documented xr-sim alias acquires it; moving only the support controller steers the native aim direction and visible gun/hand together. Release restores one-hand aim; empty-air, modal and wrench controls refuse support.',
 evidence_grade='LIVE',environment='in_game',
 method='MSVC Release, CMake/CTest (40/40), Python 3.12 comparison; injected Steam Prey under process-scoped xr-sim and xr-tape. See docs/TWO-HANDED-AIM-2026-09-14.md. Installed simulator ignores float-action subactionPath; right squeeze is explicitly a harness alias, not independent physical-controller proof.',
 validity='VALID',mutation='temporary',fact_verdict='CONFIRM',
 baseline={'verdict':'FAIL','reason':'Feature controls and all 40 tests pass, but captured baseline has an unresolved horizontal rendering band already present before acquisition. Complete renderer/headset acceptance is not established.'},
 limits='Only GLOO support and wrench exclusion observed live. No projectile fired; no physical-controller separation, headset comfort, shotgun/Q-Beam coverage or broad renderer acceptance. Geometry is derived from native wrist positions, not authored per-weapon contact metadata. Tape capped at 20000 frames; later evidence uses mod reports and xr-sim captures.',
 routes=['HAND-017'],
 do_not_repeat=[{'approach':'Treating xr-sim grip l state as proof that Prey received a left-only squeeze',
  'reason':'Installed simulator caches one shared float-action value and ignores requested subactionPath. Left rig=1 produced Prey squeeze=0; right rig=1 produced the positive control.',
  'reopen_when':'The installed runtime supplies per-subaction action state, or separate left/right actions are independently validated.'}],
 next_action='Headset-check GLOO grip feel and explicit recenter chord; collect native support diagnostics for shotgun and Q-Beam. Repair/validate xr-sim subaction semantics before claiming independent controller input tests.')
for key,reason in {
 'identity':'Target module hash verified; tested injected DLL and launch manifest preserved.',
 'instrument':'Prey diagnostic measures actual squeeze and weapon-local hand position. Simulator alias is identified and accounted for; pixels and native ray reports independently change.',
 'control':'Primary pose fixed across held, steered and released reports. Empty-air/held-drift refuse acquisition; inventory cancels and wrench has no support region.',
 'scene':'Loaded Talos Lobby, observed GLOO gun and wrench in retained captures.',
 'restore':'Cleared simulated inputs, disabled VR, observed torn_down in both logs, terminated only owned PID38828 and verified absence; runtime selection was process scoped.'
}.items():r['gates'][key]={'status':'PASS','reason':reason}
r['artifacts']=[{'path':str(p.relative_to(root)).replace('\\','/'),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'role':'frozen native support evidence or tested implementation'} for p in sorted(out.iterdir()) if p.is_file()]
doc=root/'docs/TWO-HANDED-AIM-2026-09-14.md'
r['artifacts'].append({'path':str(doc.relative_to(root)).replace('\\','/'),'sha256':hashlib.sha256(doc.read_bytes()).hexdigest(),'role':'method, result and limits'})
receipt.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(result,indent=2))
print('Frozen artifacts:',len(r['artifacts']))
