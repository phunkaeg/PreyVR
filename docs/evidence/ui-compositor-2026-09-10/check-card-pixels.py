"""Compare the exact pixels behind the historical missing-card claims.

Only fixed-head, fixed-layout captures enter a hash comparison. Full images and
their sidecars remain evidence; viewing a rescaled preview is not the check.
"""
from pathlib import Path
from PIL import Image
import hashlib,json
root=Path(__file__).resolve().parents[3]
dest=Path(__file__).resolve().parent
sets={
 'candidate06':root/'docs/evidence/vr-interface-2026-09-09/private-live-xrsim-06/capture',
 'candidate07':root/'docs/evidence/vr-interface-2026-09-09/private-live-xrsim-07/capture',
 'followup':dest/'baseline-sim/capture',
}
rows=[];groups={};skipped=[]
for label,folder in sets.items():
 for sidecar in sorted(folder.glob('*.json')):
  data=json.loads(sidecar.read_text());layers=data.get('layers',[])
  if len(layers)!=2 or any(x['type']!='quad' for x in layers):continue
  if data['head']['pos']!=[0,1.6,0] or data['head']['quat']!=[0,0,0,1]:
   skipped.append({'name':sidecar.stem,'reason':'head/layout outside fixed reference'});continue
  layout=[(x['sizeM'],x['pose'],x['premultiplied']) for x in layers]
  key=json.dumps([data['width'],data['height'],data['views'],layout],sort_keys=True)
  for eye,box in [('left',(395,633,832,695)),('right',(200,633,637,695))]:
   p=sidecar.with_name(sidecar.stem+'_'+eye+'.png')
   if not p.is_file():continue
   im=Image.open(p).convert('RGB');assert im.size==(1032,1104)
   crop=im.crop(box);sha=hashlib.sha256(crop.tobytes()).hexdigest()
   row={'set':label,'name':sidecar.stem,'eye':eye,'box':box,
        'image':p.relative_to(root).as_posix(),'imageSha256':hashlib.sha256(p.read_bytes()).hexdigest(),
        'sidecar':sidecar.relative_to(root).as_posix(),'sidecarSha256':hashlib.sha256(sidecar.read_bytes()).hexdigest(),
        'cardPixelSha256':sha,'brightPixels':sum(max(px)>100 for px in crop.getdata())}
   rows.append(row);groups.setdefault((key,eye),[]).append(row)
required={( 'candidate07',name,eye) for name in
          ['main-menu-07','gameplay-07','actual-inventory-07','guide-probe-07']
          for eye in ['left','right']}
observed={(r['set'],r['name'],r['eye']) for r in rows}
if not groups or not required.issubset(observed):
 print('NO_DATA: a required historical control image or layout is missing')
 raise SystemExit(2)
summary=[]
for (key,eye),group in groups.items():
 hashes=sorted(set(x['cardPixelSha256'] for x in group))
 summary.append({'eye':eye,'count':len(group),'hashes':hashes,'allIdentical':len(hashes)==1,
                 'members':[(x['set'],x['name']) for x in group]})
result={'question':'Do the historically labelled missing-text captures actually have missing card pixels?',
        'rows':rows,'groups':summary,'skipped':skipped,
        'allFixedLayoutGroupsIdentical':all(x['allIdentical'] for x in summary)}
(dest/'card-pixel-verification.json').write_text(json.dumps(result,indent=2))
print(json.dumps({'comparedEyeImages':len(rows),'groupSizes':[x['count'] for x in summary],
                 'allFixedLayoutGroupsIdentical':result['allFixedLayoutGroupsIdentical'],
                 'brightPixelCounts':sorted(set(x['brightPixels'] for x in rows)),
                 'skippedDifferentHeadLayout':len(skipped)},indent=2))
if not result['allFixedLayoutGroupsIdentical']:raise SystemExit(1)
