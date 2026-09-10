"""Independent simulator aiming helper; identity-oriented panel captures only."""
import json
import math
from pathlib import Path
import sys

d = json.loads(Path(sys.argv[1]).read_text())
x, y = float(sys.argv[2]), float(sys.argv[3])
hand = sys.argv[4] if len(sys.argv)>4 else "r"
assert max(abs(v) for v in d["head"]["ypr"]) < .001, "Recentered rotation needs full layer quaternion metadata"
p = d["layers"][0]
assert p["type"] in ("quad", "cylinder")
u,v=x/2560,y/1440 # This run's independently confirmed native backbuffer.
width,height=p["sizeM"]
px,py,pz=p["pose"]
if p["type"] == "cylinder":
    angle=(u-.5)*p["centralAngle"]
    px += p["radius"]*math.sin(angle)
    pz -= p["radius"]*math.cos(angle)
else:
    px += (u-.5)*width
py += (.5-v)*height
ox,oy,oz=d["handR" if hand=="r" else "handL"]["grip"]
dx,dy,dz=px-ox,py-oy,pz-oz
yaw=math.degrees(math.atan2(-dx,-dz))
pitch=math.degrees(math.atan2(dy,math.hypot(dx,dz)))
print(f"hand {hand} point {yaw:.6f} {pitch:.6f}")
