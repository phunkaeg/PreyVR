"""Check retained native no-alias observations; never launches a game."""
import argparse
import json
import math
import re
from pathlib import Path


def angle(a, b):
    dot = sum(x*y for x, y in zip(a, b))
    norm = math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
    return math.degrees(math.acos(max(-1, min(1, dot/norm))))


def check(root):
    phases = ('baseline', 'right-only', 'held-settled', 'steered', 'released')
    reports, states = {}, {}
    for phase in phases:
        raw = (root / f'{phase}.txt').read_text(encoding='utf-8-sig')
        # Each file has the command reply followed by the native render report.
        reports[phase] = dict(re.findall(r'(\w+)=([^\s]+)', raw))
        if phase != 'baseline':
            states[phase] = json.loads((root / f'{phase}-state.json').read_text(encoding='utf-8-sig'))
    for phase, expected in (('baseline', 0), ('right-only', 0), ('held-settled', 1), ('steered', 1), ('released', 0)):
        assert int(reports[phase]['held']) == expected, phase
        assert int(reports[phase]['regionReady']) == 1 and int(reports[phase]['usable']) == 1, phase
    for phase in ('held-settled', 'steered', 'released'):
        assert states[phase]['controls']['gripR'] == 0, phase
        assert states[phase]['controls']['gripL'] == (0 if phase == 'released' else 1), phase
        assert states[phase]['errors'] == 0, phase
    assert states['right-only']['controls']['gripR'] == 1 and states['right-only']['controls']['gripL'] == 0
    assert float(reports['right-only']['squeeze']) == 0
    assert int(reports['held-settled']['frames']) >= 1800
    assert int(reports['steered']['frames']) > int(reports['held-settled']['frames'])
    def vector(phase, key):
        return [float(v) for v in reports[phase][key].split(',')]
    for phase in ('held-settled', 'steered', 'released'):
        assert vector(phase, 'rpRawPos') == vector('baseline', 'rpRawPos'), phase
        assert vector(phase, 'rpRawQ') == vector('baseline', 'rpRawQ'), phase
        assert states[phase]['head'] == states['held-settled']['head'], phase
        assert states[phase]['handR'] == states['held-settled']['handR'], phase
    assert states['steered']['handL']['pos'] != states['held-settled']['handL']['pos']
    steering = angle(vector('held-settled', 'rpDir'), vector('steered', 'rpDir'))
    restoration = angle(vector('baseline', 'rpDir'), vector('released', 'rpDir'))
    assert steering > 10 and restoration < .1
    late = {}
    for phase in ('empty-air', 'return-held', 'reacquired', 'inventory'):
        raw = (root / f'{phase}.txt').read_text(encoding='utf-8-sig')
        fields = dict(re.findall(r'(\w+)=([^\s]+)', raw))
        late[phase] = {'verdict': 'INCONCLUSIVE', 'regionReady': int(fields['regionReady']), 'reason': 'fresh native support geometry unavailable'}
        assert int(fields['regionReady']) == 0
    assert 'No running Prey: True' in (root / 'shutdown.txt').read_text()
    return {'positiveContract': 'PASS', 'leftOnlyHeldAppliedCounter': int(reports['held-settled']['frames']),
            'steeringDegrees': steering, 'releasedDifferenceFromBaselineDegrees': restoration,
            'lateControls': late, 'headsetAcceptance': 'NOT_MEASURED', 'xrTape': 'NOT_ENABLED'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, default=Path(__file__).resolve().parents[2] / 'docs/evidence/twohand-no-alias-2026-10-04')
    args = parser.parse_args()
    print(json.dumps(check(args.evidence), indent=2))
