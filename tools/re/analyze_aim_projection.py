"""Read saved report lines; never connects to Prey or writes a command channel.

Input: a log/text file containing coherent reticleProjection/rp* fields.
Output: JSON separating raw tracking, world-ray composition and projection.
Movie pixels are not present in these reports and cannot be verified here.
"""
import argparse
import json
import math
from pathlib import Path


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(a):
    length = math.sqrt(dot(a, a))
    if not math.isfinite(length) or length < 1e-10:
        raise ValueError("invalid vector/quaternion")
    return [x / length for x in a]


def angle(a, b, quaternion=False):
    cosine = dot(unit(a), unit(b))
    if quaternion:
        cosine = abs(cosine)  # q and -q describe the same rotation
    return math.degrees(math.acos(max(-1, min(1, cosine)))) * (2 if quaternion else 1)


def values(row, key, count):
    result = [float(x) for x in row[key].split(',')]
    if len(result) != count or not all(math.isfinite(x) for x in result):
        raise ValueError(f"invalid {key}")
    return result


def inspect(row):
    origin, direction = values(row, 'rpOrigin', 3), values(row, 'rpDir', 3)
    matrix = values(row, 'rpCamera', 12)
    left, right, down, up = values(row, 'rpTans', 4)
    distance = float(row['rpDistance'])
    if not math.isfinite(distance) or distance <= 0 or not (right > left and up > down):
        raise ValueError('invalid convergence/frustum')
    relative = [origin[i] + direction[i] * distance - matrix[i * 4 + 3] for i in range(3)]
    axes = [[matrix[i * 4 + column] for i in range(3)] for column in range(3)]
    cam_x, cam_forward, cam_up = [dot(relative, axis) for axis in axes]
    if cam_forward <= 0:
        raise ValueError('successful projection record points behind camera')
    projected = [(cam_x / cam_forward - left) / (right - left),
                 (up - cam_up / cam_forward) / (up - down)]
    raw = values(row, 'rpRawXY', 2)
    xy = values(row, 'rpXY', 2)
    x, y, z, w = unit(values(row, 'rpRawQ', 4))
    # Quaternion rotation's -Z column gives the OpenXR pointing direction.
    xr_forward = [-2 * (x * z + y * w), -2 * (y * z - x * w), -(1 - 2 * (x*x + y*y))]
    ex, ey, ez = xr_forward[0], -xr_forward[2], xr_forward[1]
    yaw = float(row['rpPlayYaw'])
    if not math.isfinite(yaw):
        raise ValueError('invalid play yaw')
    expected_dir = [math.cos(yaw)*ex - math.sin(yaw)*ey,
                    math.sin(yaw)*ex + math.cos(yaw)*ey, ez]
    return {
        'projection_residual_milli': max(abs(a-b) for a, b in zip(projected, raw)) * 1000,
        'clamp_residual_milli': max(abs(max(0, min(1, a))-b) for a, b in zip(raw, xy)) * 1000,
        'composition_residual_degrees': angle(expected_dir, direction),
        'basis_orthonormal_error': max(abs(dot(a, b) - int(i == j))
            for i, a in enumerate(axes) for j, b in enumerate(axes)),
    }


def analyze(text):
    rows, skipped, seen = [], 0, set()
    errors = []
    for line_number, line in enumerate(text.splitlines(), 1):
        row = dict(token.split('=', 1) for token in line.split() if '=' in token)
        if 'reticleProjection' not in row:
            continue
        if row['reticleProjection'] != 'fresh' or row.get('rpContext') != '1':
            skipped += 1
            continue
        try:
            identity = (row['rpStampNs'], row['rpIndex'])
            if identity in seen:
                continue
            metrics = inspect(row)
            for key, size in [('rpHeadQ', 4), ('rpRawQ', 4), ('rpDir', 3), ('rpOrigin', 3)]:
                values(row, key, size)
            unit(values(row, 'rpHeadQ', 4))
            for key in ['rpEpoch', 'rpRef', 'rpIndex', 'rpStampNs']:
                int(row[key])
            for key in ['rpClamped', 'rpDispatch']:
                if row[key] not in ('0', '1'):
                    raise ValueError(f'invalid {key}')
            values(row, 'rpDispatchResults', 2)
            rows.append((row, metrics))
            seen.add(identity)
        except (KeyError, ValueError, ZeroDivisionError) as error:
            errors.append({'line': line_number, 'error': str(error)})
    result = {'records': len(rows), 'skipped_unavailable_stale_or_no_context': skipped,
              'malformed_records': errors,
              'scope': 'Numerical dispatch inputs only; no movie-pixel or headset acceptance.'}
    # Never compare directions across recentres or tracking restarts as drift.
    groups = {}
    for row, metrics in rows:
        groups.setdefault((row['rpEpoch'], row['rpRef']), []).append((row, metrics))
    result['groups'] = []
    for (epoch, reference), samples in groups.items():
        first = samples[0][0]
        group = {'tracking_epoch': epoch, 'reference_generation': reference, 'records': len(samples)}
        for name, key, quaternion in [('head_rotation_from_first_deg', 'rpHeadQ', True),
                                      ('controller_rotation_from_first_deg', 'rpRawQ', True),
                                      ('world_direction_from_first_deg', 'rpDir', False)]:
            size = 4 if quaternion else 3
            group['max_' + name] = max(angle(values(first, key, size), values(row, key, size), quaternion)
                                      for row, _ in samples)
        for key in samples[0][1]:
            group['max_' + key] = max(metrics[key] for _, metrics in samples)
        group['clamped_records'] = sum(row['rpClamped'] == '1' for row, _ in samples)
        group['dispatch_disabled_records'] = sum(row['rpDispatch'] == '0' for row, _ in samples)
        group['nonzero_dispatch_results'] = sum(row['rpDispatchResults'] != '0,0' for row, _ in samples)
        result['groups'].append(group)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report_file', type=Path)
    args = parser.parse_args()
    result = analyze(args.report_file.read_text(encoding='utf-8-sig'))
    print(json.dumps(result, indent=2))
    raise SystemExit(1 if not result['records'] or result['malformed_records'] else 0)
