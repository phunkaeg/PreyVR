from pathlib import Path
from datetime import datetime, timezone
import subprocess
import hashlib
import json

out = Path(__file__).resolve().parent
root = out.parents[2]
donor = Path('D:/Dev Debug/Other VR Mods/prey-vr')
def read(path):
    return path.read_text(encoding='utf-8-sig')
anchors = {
    root/'src/dll/XrSessionHost.cpp': ['declaredFov ? *declaredFov : views[target].fov', 'hudLayer.space=gHost.space'],
    root/'src/dll/ConsoleBridgeWin32.cpp': ['execute(console, command.c_str(), false, true)', 'queued_to_engine'],
    root/'src/dll/XrInput.cpp': ['"squeeze"', 'spaceInfo.subactionPath'],
    root/'tools/Start-PreyVR.ps1': ['[int]$Width=2016', '[int]$Height=2160'],
    donor/'src/Src/ModMain.cpp': ['viewPitchDeg += RAD2DEG(headPitch)', '* m_Ipd * m_WorldScale', 'bool ModMain::UpdateWheelPointing'],
    donor/'src/Src/XRCameraHook.cpp': ['Ang3 add(DEG2RAD(pitch), 0.0f, DEG2RAD(yaw))'],
    donor/'mockxr/mock_runtime.cpp': ['a->name == "grip_l"', "a->name.back() == 'r'"],
}
evidence = []
for path, snippets in anchors.items():
    txt = read(path)
    for snippet in snippets:
        assert snippet in txt, (path, snippet)
    evidence.append({'file': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                     'anchors': [{'text': a, 'line': txt[:txt.index(a)].count('\n')+1} for a in snippets]})
branch = subprocess.run(['git', '-C', str(donor), 'merge-base', '--is-ancestor', 'origin/stage-6-hands', 'HEAD'])
assert branch.returncode == 0
tests = subprocess.run(['C:/Program Files/CMake/bin/ctest.exe', '--test-dir', str(root/'build/inventory-stereo'), '-C', 'Release', '-N'], capture_output=True, text=True)
assert tests.returncode == 0 and 'Total Tests: 40' in tests.stdout
report = root/'docs/PREY-VR-COMPARISON-2026-09-16-v2.md'
assert report.exists() and 'identified, not fixed in code' in read(report)
checks = {'recorded_at': datetime.now(timezone.utc).isoformat(), 'source_evidence': evidence,
          'hands_branch_is_ancestor_exit': branch.returncode,
          'ctest_enumeration_only': tests.stdout,
          'limits': 'Source anchors plus git ancestry and test enumeration; no test execution, build, game or headset session.'}
(out/'peer-checks.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
r = json.loads(read(root/'receipts/20260916-source-comparison.json'))
r.update({'run_id': '20260916-source-comparison-v2', 'recorded_at': checks['recorded_at'],
    'question': 'Which claims in the peer comparison correct or contradict the original source review?',
    'claim': 'Peer review correctly identifies P FOV fallback and omitted J head-roll/runtime-IPD and HUD-space distinctions. Current source/git contradict its unmerged-branch, fixed-resolution and drop-in-mock assumptions; v2 corrects the plan and scopes evidence and allocations.',
    'method': 'Direct source/caller reads; git merge-base --is-ancestor; CTest -N enumeration only. Reproduce with docs/evidence/prey-vr-comparison-20260916/check_peer.py. Original SOURCE receipt preserved unchanged.',
    'limits': 'Scores and allocations remain reviewer judgments. FOV defect identified, not fixed or observed live. No new runtime, build, test pass or performance claim. Revision supersedes recommendations in 20260916-source-comparison, not its frozen evidence.',
    'next_action': 'Review v2 integration plan and peer corrections; first implementation work should enforce valid rendered FOV and coherent image provenance. Original receipt remains an immutable historical review.',
    'artifacts': [{'path': p, 'role': role} for p, role in [
        ('docs/PREY-VR-COMPARISON-2026-09-16-v2.md', 'Corrected plan and detailed peer claim assessment'),
        ('docs/evidence/prey-vr-comparison-20260916/peer-checks.json', 'Source hashes/anchors, git ancestry, test enumeration'),
        ('docs/evidence/prey-vr-comparison-20260916/check_peer.py', 'Reproduction script'),
        ('docs/evidence/prey-vr-comparison-20260916/revise_from_peer.py', 'Revision generator preserving original report')]]})
r['gates']['control'] = {'status': 'PASS', 'reason': 'Checked alleged unmerged branch with git ancestry and source presence; configured tests by enumeration; inspected actual action names and callers instead of relying on comments.'}
(root/'receipts/20260916-source-comparison-v2.json').write_text(json.dumps(r, indent=2), encoding='utf-8')
print('PASS: source anchors, ancestry, 40-test enumeration; v2 receipt ready to stamp.')
