from pathlib import Path
import json

root = Path(__file__).resolve().parents[3]
path = root / 'receipts/20260916-source-comparison.json'
r = json.loads(path.read_text(encoding='utf-8-sig'))
r.update({
    'target': {'name': 'J prey-vr current source compared with P PreyVR development source (manifest identifies both)',
               'identity_kind': 'git_commit', 'identity': 'b680f8bcb70d2deb6910c497d38e54d26683f933'},
    'question': 'Do these two current codebases already contain complementary integration mechanisms, or can one be adopted wholesale?',
    'claim': 'Source inspection finds complementary capabilities: P original-rig/two-hand/UI contracts and J locomotion/crouch/psi/weapon-consumer mechanisms. Both normal stereo paths alternate eyes; both attach newly located XR poses at completed-image publication rather than carrying a complete source render record. A wholesale merge does not solve shared provenance and unfinished-feature gaps.',
    'evidence_grade': 'SOURCE', 'environment': 'source',
    'method': 'Read-only git/source inspection and existing P graph query. Reproduce identity, 20 source anchors and three synthetic crop arithmetic controls with Python 3.12: python -E -B docs/evidence/prey-vr-comparison-20260916/audit.py. Numeric controls are not compiled target execution. See comparison report for ranking method and source map.',
    'validity': 'VALID', 'mutation': 'none', 'fact_verdict': 'CONFIRM',
    'baseline': {'verdict': 'NOT_MEASURED', 'reason': 'No new build, game, GPU benchmark or headset session. Historical P baseline band and J reported pairing gaps remain separately documented.'},
    'limits': 'Feature scores and proposed developer allocation are reviewer judgments, not experimental performance results. Donor runtime reports remain author evidence. J binary ABI/hash not independently verified; P tree is dirty and identified by source hashes. Private branches outside local refs not assessed. Neither successful synthetic math nor source anchors establishes visual quality.',
    'artifacts': [{'path': p, 'role': role} for p, role in [
        ('docs/PREY-VR-COMPARISON-2026-09-16.md', 'Source review, ranked feature comparison, integration dependency graph and proposed ownership'),
        ('docs/evidence/prey-vr-comparison-20260916/manifest.json', 'Git identities and hashes of source inventory; not copied donor code'),
        ('docs/evidence/prey-vr-comparison-20260916/checks.json', 'Source locations and synthetic crop controls with explicit limits'),
        ('docs/evidence/prey-vr-comparison-20260916/audit.py', 'Reproduction script')]],
    'routes': [], 'do_not_repeat': [],
    'next_action': 'Review the comparison for Prey fleet integration guidance; agree reuse terms, target adapter and coherent render/gameplay contracts before adapting features. Do not promote scores or author HMD claims into runtime acceptance.'
})
r['gates'] = {
    'identity': {'status': 'PASS', 'reason': 'Both full git commits, donor clean status, P dirty status and source-file SHA-256 inventory recorded.'},
    'instrument': {'status': 'PASS', 'reason': 'Direct source reads checked call paths beyond comments; git/graph leads cross-checked; 20 anchors and three arithmetic controls reproduced.'},
    'control': {'status': 'PASS', 'reason': 'Compared current callers/defaults with older docs and helper-only features. Crop arithmetic has exact-aligned positive control, odd-resolution case and deliberate missing-coverage case.'},
    'scene': {'status': 'NA', 'reason': 'Source comparison; no live scene required or claimed.'},
    'restore': {'status': 'NA', 'reason': 'No game/runtime/source implementation mutation. Only local review artifacts added.'}
}
path.write_text(json.dumps(r, indent=2), encoding='utf-8')
print('Completed source comparison receipt; stamping still required.')
