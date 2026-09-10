"""Remove common panel tilt before deciding whether inventory planes differ."""
import collections, json, math
from pathlib import Path

root=Path(__file__).parent
data=json.loads((root/'live-depth-02.json').read_text())
frames=collections.defaultdict(list)
for row in data['matrices']: frames[row['display']].append(row['m'])
summary=[]
for frame, matrices in frames.items():
    # Row-vector 4x4: transformed local X/Y/Z axes occupy rows 0/1/2,
    # translation row 3. Project each origin onto the shared unit plane normal.
    normal=matrices[0][8:11]
    length=math.sqrt(sum(v*v for v in normal))
    normal=[v/length for v in normal]
    depths=[]
    for m in matrices:
        assert all(math.isfinite(v) for v in m)
        assert max(abs(m[8+i]/math.sqrt(sum(x*x for x in m[8:11]))-normal[i]) for i in range(3))<1e-5
        for axis in [m[0:3],m[4:7]]:
            assert abs(sum(axis[i]*normal[i] for i in range(3)))<1e-5
        depths.append(sum(m[12+i]*normal[i] for i in range(3)))
    # Positive tilted-plane control: changing local X/Y alone must NOT change depth.
    m=matrices[0]; origin=m[12:15]
    moved=[origin[i]+321*m[i]+654*m[4+i] for i in range(3)]
    assert abs(sum((moved[i]-origin[i])*normal[i] for i in range(3)))<.01
    groups=collections.Counter(round(z-depths[0]) for z in depths)
    error=max(abs((z-depths[0])-round(z-depths[0])) for z in depths)
    assert error<.01
    summary.append({'display':frame,'unique_matrices':len(matrices),'normal':normal,
      'relative_plane_offsets_native_units':dict(sorted(groups.items())),
      'maximum_quantization_residual':error})
out={'method':'common-normal projection removes shared panel tilt; source translation is not metres',
     'tilted_plane_control':'PASS','frames':summary}
(root/'depth-analysis.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
