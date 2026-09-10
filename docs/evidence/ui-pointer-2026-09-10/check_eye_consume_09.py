"""Static placement check; not a rendered-camera or headset acceptance test."""
import json
import re
import zipfile
from pathlib import Path

root=Path(__file__).resolve().parents[3]
evidence=Path(__file__).resolve().parent
def check(source):
    source=re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S)
    start=source.index('void ServiceXrFrame(void* renderer)')
    body=source[source.index('{',start)+1:]
    first=body[:body.index(';')+1].strip()
    return (first=='const int renderedEye = dll::ConsumeRenderedEye();'
            and source.count('dll::ConsumeRenderedEye()')==1
            and 'const int eye = renderedEye;' in source
            and 'frameState.predictedDisplayTime, renderedEye);' in body)

with zipfile.ZipFile(evidence/'source-08-owned.zip') as z:
    previous=z.read('src/dll/XrSessionHost.cpp').decode()
current=(root/'src/dll/XrSessionHost.cpp').read_text()
result={'candidate08':check(previous),'candidate09':check(current),
        'scope':'One consumption at frame-service entry before status, mutex, menu, acquisition and shouldRender branches; no second consumption in stereo-copy helper. Does not prove native producer/renderer correspondence.'}
assert result['candidate08'] is False and result['candidate09'] is True
(evidence/'eye-consume-placement-09.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
