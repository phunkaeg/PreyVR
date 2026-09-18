import json, pathlib, struct
import numpy as np
from PIL import Image

root = pathlib.Path('build/inventory-stereo/Release')
out = pathlib.Path('build/inventory-stereo-live/decoded')
out.mkdir(exist_ok=True)
regions = {'tab': (700,55,940,100), 'grid': (120,195,910,630), 'title': (1230,210,1790,250), 'description': (1230,295,1790,458), 'footer': (90,980,375,1020)}
results = []
for tag, depth in [(1100,25),(1200,0),(1300,100)]:
    paths = [next(root.glob(f'*tag{tag+eye}-eye{eye}.pvrframe')) for eye in range(2)]
    arrays, headers = [], []
    for p in paths:
        data = p.read_bytes()
        h = struct.unpack('<8s6I2Q',data[:48])
        assert h[0] == b'PVRFRAME' and h[4] == 28 and len(data) == 48+h[3]*h[5]
        a = np.frombuffer(data[48:],np.uint8).reshape(h[3],h[5])[:,:h[2]*4].reshape(h[3],h[2],4)
        arrays.append(a); headers.append(h)
        Image.fromarray(a).save(out/(p.stem+'.png'))
    assert headers[0][7] == headers[1][7]
    l,r = arrays
    delta = np.abs(l.astype(float)-r.astype(float))
    result = dict(depth=depth, frame=headers[0][7], files=[str(p) for p in paths], identical=bool(np.array_equal(l,r)), mean_absolute=float(delta.mean()), changed_fraction=float(np.any(delta>0,axis=2).mean()), regions={})
    for name,(x0,y0,x1,y1) in regions.items():
        a = l[y0:y1,x0:x1,:3].astype(float)
        scores=[]
        for shift in range(-16,17):
            b=r[y0:y1,x0+shift:x1+shift,:3].astype(float)
            scores.append((float(np.abs(a-b).mean()),shift))
        score,shift=min(scores)
        result['regions'][name]={'right_minus_left_pixels':shift,'mae':score,'zero_shift_mae':dict((s,e) for e,s in scores)[0]}
    results.append(result)
(out/'comparison.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
