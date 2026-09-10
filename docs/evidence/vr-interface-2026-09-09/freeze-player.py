"""Freeze this inspected player candidate and prepare its two bounded receipts."""
from pathlib import Path
import hashlib, json, shutil, zipfile

root = Path(__file__).resolve().parents[3]
evidence = Path(__file__).resolve().parent
frozen = evidence / 'final-player'
frozen.mkdir(exist_ok=False)
package = root / 'build/packages/PreyVR-player-preview-20260910'
manifest = json.loads((package / 'manifest.json').read_text(encoding='utf-8-sig'))
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
for entry in manifest['files']:
    p = package / entry['path']
    assert p.stat().st_size == entry['bytes'] and digest(p) == entry['sha256'], p
assert not (package / 'PreyVR.json').exists(), 'Do not ship machine-specific settings'
archive = package.with_suffix('.zip')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert all('PreyVR.json' not in name and 'xrsim' not in name.lower() for name in z.namelist())
    for entry in manifest['files']:
        data=z.read(package.name+'/'+entry['path'])
        assert hashlib.sha256(data).hexdigest()==entry['sha256'], entry['path']
shutil.copyfile(archive,frozen / archive.name)
shutil.copyfile(package / 'manifest.json', frozen / 'package-manifest.json')
paths = {root/'CMakeLists.txt', root/'README.md', root/'docs/PLAYER-GUIDE.md'}
for folder in ['src','include','tests','tools/injector']:
    paths.update(p for p in (root/folder).rglob('*') if p.is_file())
for name in ['Start-PreyVR.ps1','Start Prey VR.cmd','Package-PreyVR.ps1','Invoke-PreyVRStartup.ps1']:
    paths.add(root/'tools'/name)
with zipfile.ZipFile(frozen/'source-snapshot.zip','x',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(paths):z.write(p,p.relative_to(root).as_posix())
summary = {'dllSha256':digest(package/'PreyVR.dll'),'packageSha256':digest(archive),
           'sourceZipSha256':digest(frozen/'source-snapshot.zip'),
           'sourceFiles':[{ 'path':p.relative_to(root).as_posix(),'sha256':digest(p)} for p in sorted(paths)],
           'verifiedPackageFiles':len(manifest['files']), 'privateRuntimePackaged':False,
           'machineSettingsPackaged':False}
(frozen/'verification.json').write_text(json.dumps(summary,indent=2))

def artifact(path,role):
    p=root/path
    assert p.is_file() and p.stat().st_size>0,p
    return {'path':path,'sha256':digest(p),'role':role}
prefix=evidence.relative_to(root).as_posix()+'/'
common=['docs/RE-VR-INTERFACE-2026-09-10.md','docs/PLAYER-GUIDE.md']
common += [prefix+x for x in ['target-info.json','private-runtime.patch','private-runtime-source.json',
    'ctest-private-runtime.txt','build-player-07-final.txt','ctest-player-07-final.txt',
    'final-active-report-07.txt','held-exit-before-06.txt','held-exit-after-06.txt',
    'loss-menu-06.txt','loss-gameplay-06.txt','recovered-gameplay-06.txt',
    'runs/player-20260910-021201/PreyVR.log','runs/player-20260910-023114/PreyVR.log',
    'tape-check-06-0.json','tape-check-06-1.json','tape-check-06-2.json',
    'final-player/verification.json','final-player/package-manifest.json','final-player/source-snapshot.zip',
    'final-player/PreyVR-player-preview-20260910.zip','launcher-final-preflight.txt']]
for p in (evidence/'private-live-tape-06').glob('*.ndjson'):common.append(p.relative_to(root).as_posix())
hud=['flash-0x180e8c5e0.c','flash-bindings-gameplay.json','flash-bindings-inventory.json',
     'visible-getter.json','hud-alpha-check.json','hud-raw/frame-14999-tag0.pvrframe',
     'hud-raw/frame-14999-tag0.png','guide-texture-07.json','guide-texture-07.bgra','guide-texture-07.png',
     'd3d-readback-slots.json','read-guide-texture.py']
for state,name in [('02','hud-layer-first'),('02','hud-off-control'),('06','held-exited-06'),
                   ('06','inventory-page-06'),('06','menu-recentered-06'),('07','final-gameplay-07'),
                   ('07','guide-probe-07'),('07','actual-inventory-07')]:
    hud += [f'private-live-xrsim-{state}/capture/{name}{ext}' for ext in ['.json','_left.png','_right.png']]
pose=['mount-live-03.json','attachment-name-getters.json','wrench-on-close-live-04.json',
      'disruptor-layout-live-04.json','helper-pose-disruptor-05.json','gloo-alignment-06.txt','wrench-alignment-06.txt']
for name in ['gameplay-forward-06','gloo-angle-06','gloo-forward-06','wrench-selected-06','wrench-angle-06','wrench-forward-06']:
    pose += [f'private-live-xrsim-06/capture/{name}{ext}' for ext in ['.json','_left.png','_right.png']]

for run_id,extra in [('20260910-native-hud-layer',hud),('20260910-pose-slice-correction',pose)]:
    path=root/'receipts'/f'{run_id}.json';r=json.loads(path.read_text())
    r.update(target={'name':'Steam PreyDll.dll; player candidate 07 and frozen source snapshot',
                     'identity_kind':'sha256','identity':manifest['supportedPreyDll']},
        evidence_grade='LIVE',environment='in_game',validity='VALID',mutation='persistent',
        baseline={'verdict':'FAIL','reason':'33/33 offline tests and the measured player flow passed, but the simulator control card remains intermittent, broad trace checks are not all green, and headset acceptance is open. This receipt does not approve production readiness.'},
        limits='Actual injected Steam Prey with a private xr-sim validation runtime. No physical Quest/VDXR acceptance, all-weapon/terminal coverage, native collision call or frame-time claim. Read the report for capped traces, native-versus-optical FOV policy, transition omissions and the unresolved card capture inconsistency.',
        artifacts=[artifact(p,'Inspected evidence; exact observation scope and caveats in report') for p in common+[prefix+x for x in extra]],
        routes=[],do_not_repeat=[],next_action='Review for the UI/pose ownership chapters; test Quest/VDXR readability, animated weapons and terminals. Investigate card compositing with the preserved raw GPU texture; do not infer a missing upload from one screenshot.')
    r['gates']={
      'identity':{'status':'PASS','reason':'Steam target SHA, Ghidra program and PE getter bytes agree; final source/package hashes frozen.'},
      'instrument':{'status':'PASS','reason':'Native callback observations plus raw GPU texture and pixel captures; private runtime corrections and limits retained. Claims are bounded to the inspected native HUD/pose evidence, not full runtime conformance.'},
      'control':{'status':'PASS','reason':'Native HUD off/on and isolated alpha target; old pose-prefix failure captured, owning-count correction and refusal fixtures executed.'},
      'scene':{'status':'PASS','reason':'Actual Prey Talos Lobby save and native menus/inventory; declared simulated headset/profile.'},
      'restore':{'status':'PASS','reason':'Owned processes disarmed and closed normally; shared runtime registration/game binary unchanged. Persistent source and isolated outputs intentionally retained.'}}
    if run_id.endswith('native-hud-layer'):
        r.update(question='Can DanielleHUD render once into a private transparent target and be submitted separately without retaining its baked scene copy?',
          claim='The concrete Flash RT callback can redirect its one original DanielleHUD draw, preserving private stencil and native filter targets. In-game isolated RGBA output contains HUD graphics and transparent background; on/off captures and projection-plus-HUD submissions support removal from the scene and separate presentation. Other native UI canvases remain scoped separately.',
          fact_verdict='CONFIRM',method='Ghidra 12.1.2 static callback/receiver checks; bounded Frida read-only native callback/D3D bindings; process-scoped xr-sim, xr-tape candidate-06 traces, existing FrameCaptureWin32 GPU readback; CMake/MSVC Release and CTest 33/33. Final candidate-07 uses xr-sim captures/logs; no candidate-07 tape was produced. Reproduce with tools/Start-PreyVR.ps1 and the commands/state paths in the report.')
    else:
        r.update(question='Are CPoseData absolute QuatT slices DynArrays whose element count can be read from slice minus four?',
          claim='No. The native getter is base-at-owner+0x18 plus index*28 and the count is owner+8. The live slice-prefix word was float bits, not a count. Skeleton embedded CPoseData starts at +0x18; the corrected raw-slice readers and concrete asset policies drive the observed GLOO, Disruptor and wrench without capturing equip angle. True joint/attachment/ADIK DynArrays retain their independent prefix layout.',
          fact_verdict='REFUTE',method='Corrective follow-up to 20260909-weapon-basis-alignment. Ghidra/native PE getter bytes plus bounded Frida mount/pose/attachment capture on actual ADIK and AttachToHand callbacks. Fixture tests poison the false prefix and check owner counts, concrete getter layout and unsupported paths. Candidate-06 captures exercise GLOO and wrench equipped at nonzero controller yaw and returned forward; Disruptor uses measured fx_muzzle presentation frame.')
        r['do_not_repeat']=[{'approach':'Read slice[-4] as a count solely because nearby fields use DynArray.',
                             'reason':'CPoseData owns count and separate raw buffers; the neighboring word can be an unrelated float.',
                             'reopen_when':'A different target/build proves a different owning layout and its concrete getter.'}]
    path.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='sourceFiles'},indent=2))
