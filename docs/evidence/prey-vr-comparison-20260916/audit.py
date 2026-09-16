"""Reproduce source identity and bounded arithmetic checks, not game acceptance."""
from pathlib import Path
import hashlib
import json
import math
import subprocess
from datetime import datetime, timezone

OUT = Path(__file__).resolve().parent
P = OUT.parents[2]
J = Path('D:/Dev Debug/Other VR Mods/prey-vr')

def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

def inventory(root, dirs):
    result = []
    for directory in dirs:
        base = root / directory
        paths = [base] if base.is_file() else sorted(base.rglob('*'))
        for path in paths:
            if path.is_file() and path.suffix.lower() in ('.cpp', '.h', '.md', '.txt', '.ps1', '.json'):
                data = path.read_bytes()
                result.append({'path': path.relative_to(root).as_posix(),
                               'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    return result

manifest = {
    'recorded_at': datetime.now(timezone.utc).isoformat(),
    'scope': 'Two local current checkouts; source review, no build or runtime experiment',
    'note': 'Inventory hashes identify coverage, not a claim every line was audited. No donor implementation copied.',
    'P': {'root': str(P), 'head': git(P, 'rev-parse', 'HEAD'),
          'status': git(P, 'status', '--short'),
          'files': inventory(P, ['src', 'include', 'tests', 'CMakeLists.txt',
                                'tools/Start-PreyVR.ps1', 'tools/Invoke-PreyVRLaunch.ps1'])},
    'J': {'root': str(J), 'head': git(J, 'rev-parse', 'HEAD'),
          'status': git(J, 'status', '--short'), 'branches': git(J, 'branch', '-av'),
          'authors': git(J, 'shortlog', '-sn', 'HEAD'),
          'files': inventory(J, ['src', 'mockxr', 'scripts', 'docs', 'README.md', 'CLAUDE.md'])},
}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')

checks = []
def source_check(root, filename, patterns):
    text = (root / filename).read_text(encoding='utf-8-sig')
    for pattern in patterns:
        assert pattern in text, (filename, pattern)
        checks.append({'file': str(root / filename), 'anchor': pattern,
                       'line': text[:text.index(pattern)].count('\n') + 1,
                       'result': 'anchor present; interpretation requires surrounding source'})

source_check(P, 'src/dll/AimTakeover.cpp', ['gOriginMode{0}', 'SolveTwoHandedFrame', 'SetAimOriginFromHand'])
source_check(P, 'src/dll/ReticleFollow.cpp', ['kDefaultConvergenceMm = 10000'])
source_check(P, 'src/dll/XrSessionHost.cpp', ['gHost.eyePose[target] = views[target].pose;', 'CopyResource(gHost.eyeImage[target], backBuffer)'])
source_check(P, 'src/dll/HudLayer.cpp', ['void __fastcall StereoDisplay', 'void __fastcall CaptureFlash'])
source_check(J, 'src/Src/XRSession.cpp', ['sx = (unsigned)x0; sy = (unsigned)y0;', 'projViews[i].fov = g.views[i].fov;', 'const XrPosef& rp = g.views[i].pose;'])
source_check(J, 'src/Src/XRRenderHook.cpp', ['return m_LastEye;', 'm_Session.EndFrameNoRender();'])
source_check(J, 'src/Src/ModMain.cpp', ['void ModMain::UpdatePhysicalCrouch', 'void ModMain::UpdateTurn', 'bool ModMain::UpdateWheelPointing'])
source_check(J, 'src/Src/XRGameHooks.cpp', ['GroundMovementInput_Hook', 'PsiCamSwap'])
source_check(J, 'src/Src/XRWeaponHook.cpp', ['GetReticleInfoForFiring_Hook', 'IsMuzzleSpawn(_position) && MuzzleShot(p, d)'])

# Reproduce horizontal part of MatchedCropRect on synthetic inputs. Not compiled donor execution.
def donor_crop(width, coverage, left, right):
    x0 = max(0.0, (left + coverage) * width / (2 * coverage))
    x1 = min(float(width), (right + coverage) * width / (2 * coverage))
    assert x1 - x0 >= 16
    lo, hi = int(x0), int(x1)
    rendered = [2 * coverage * lo / width - coverage, 2 * coverage * hi / width - coverage]
    return {'width': width, 'source_tangents': [-coverage, coverage], 'requested_tangents': [left, right],
            'integer_bounds': [lo, hi], 'actual_tangents': rendered,
            'max_tangent_error': max(abs(rendered[0] - left), abs(rendered[1] - right))}

aligned = donor_crop(1000, 1, -1, 1)
odd = donor_crop(1511, 1.5, -1.0, math.tan(math.radians(50)))
missing = donor_crop(1000, 1.0, -1.3, .9)
assert aligned['max_tangent_error'] == 0
assert 0 < odd['max_tangent_error'] < 3 / 1511
assert abs(missing['max_tangent_error'] - .3) < 1e-12
result = {'evidence': 'Source anchor checks plus synthetic Python arithmetic controls only',
          'source_checks': checks, 'crop_controls': {'aligned': aligned, 'odd_resolution': odd, 'missing_coverage': missing},
          'limits': 'Does not execute donor C++, measure FPS, prove ABI, or show headset appearance.'}
(OUT / 'checks.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'P_files': len(manifest['P']['files']), 'J_files': len(manifest['J']['files']),
                  'source_anchors': len(checks), 'crop_controls': 3, 'result': 'PASS'}))
