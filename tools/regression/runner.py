"""Attach-only PreyVR visual regression runner. Default: print a plan, no IPC.

Uses existing xr-sim PowerShell helpers via bridge.ps1. The injectable backend
supports offline tests; that harness is not a native game acceptance result.
"""
from __future__ import annotations
import argparse
from contextlib import contextmanager
import hashlib
import html
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import uuid
import zlib

HERE=Path(__file__).resolve().parent
SCENARIOS=('menu','inventory','two_hand','zero_g')
TARGET='7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'

class Failure(RuntimeError): pass
class Blocked(Failure): pass
class Timeout(Failure): pass

def require(ok,message):
    if not ok: raise Failure(message)

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def dump(path,value): Path(path).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def vector(v,maximum=4):
    require(isinstance(v,list) and len(v)==3 and all(type(x) in (int,float) and math.isfinite(x) and abs(x)<=maximum for x in v),'Expected a finite bounded 3-vector')
    return v
def distance(a,b): return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
def add(a,b): return [x+y for x,y in zip(a,b)]
def text(v): return ' '.join(format(x,'.6f') for x in v)

def fixture(path):
    f=json.loads(Path(path).read_text(encoding='utf-8'))
    require(f.get('schema')==1 and isinstance(f.get('name'),str),'Unknown fixture schema/name')
    for k in ('head','left','right','headYpr','rightYpr'): vector(f[k],180 if k.endswith('Ypr') else 4)
    z=f['zeroG'];vector(z['leftAtWall']);vector(z['stroke'])
    require(.01<=distance(z['stroke'],[0,0,0])<=.18,'Physical stroke must be 1..18 cm')
    require(type(z['durationMs']) is int and 100<=z['durationMs']<=750,'Physical stroke duration must be 100..750 ms')
    require(.001<=z['minCameraMotion']<=.1,'Camera motion threshold must be 1..100 mm')
    return f

def plan(f,selected):
    return dict(mode='PLAN_ONLY',launches_game=False,writes_commands=False,fixture=f['name'],scenarios=[dict(name=n,
        prerequisite='fresh gameplay; zero_g also requires ordinary state9 and fixed geometry within reach' if n=='zero_g' else
        'fresh gameplay; two_hand also requires supported long-weapon geometry' if n=='two_hand' else 'fresh gameplay',
        captures={'menu':['before navigation','after down','after up','closed'],
                  'inventory':['before open','panel open','after tab','closed'],
                  'two_hand':['before squeeze','attached','detached'],
                  'zero_g':['before grab','anchored','after pull','before push','after push','menu cancelled']}[n]) for n in selected])

class WindowsBackend:
    environment='in_game'
    def __init__(self,args,out):
        self.out=out;self.powershell=args.powershell
        self.base=dict(schema=1,stateDir=str(Path(args.state_dir).resolve()),runDir=str(Path(args.run_dir).resolve()),
                       simRoot=str(Path(args.sim_root).resolve()),timeout=10,ownerPid=0,ownerStart='')
        for path in (Path(self.base['simRoot'])/'tools/xrsim-cmd.ps1',Path(self.base['simRoot'])/'tools/xrsim-shot.ps1',
                     Path(self.base['stateDir'])/'state.json',Path(self.base['runDir'])/'PreyVR.log'):
            require(path.is_file(),f'Missing dependency: {path}')
        self.index=0;self.module_checked=False
    def call(self,action,**fields):
        self.index+=1;request=self.out/'transport'/f'{self.index:04d}.json';request.parent.mkdir(exist_ok=True)
        dump(request,dict(self.base,action=action,**fields))
        result=subprocess.run([self.powershell,'-NoProfile','-NonInteractive','-File',str(HERE/'bridge.ps1'),'-RequestFile',str(request)],
                              text=True,encoding='utf-8',errors='replace',capture_output=True,timeout=30)
        (request.with_suffix('.stdout.txt')).write_text(result.stdout,encoding='utf-8')
        (request.with_suffix('.stderr.txt')).write_text(result.stderr,encoding='utf-8')
        try: reply=json.loads(result.stdout.strip().splitlines()[-1])
        except (ValueError,IndexError): raise Failure('Bridge did not return structured output: '+result.stderr[-500:])
        require(result.returncode==0 and reply.get('ok'),reply.get('error','Bridge failed'))
        data=reply['data']
        if action=='preflight': self.base.update(ownerPid=data['ownerPid'],ownerStart=data['ownerStart'])
        if action=='sample' and not self.module_checked:
            require(digest(data['probe']['module'])==self.expected_hash,'Live diagnostic DLL path/hash disagrees with startup identity')
            self.module_checked=True
        if action=='preflight':self.expected_hash=data['loadedDllHash']
        return data

def png_size(path):
    data=Path(path).read_bytes();require(data[:8]==b'\x89PNG\r\n\x1a\n','Missing PNG signature')
    at=8;size=None;ended=False
    while at+12<=len(data):
        length=struct.unpack_from('>I',data,at)[0];kind=data[at+4:at+8];end=at+12+length
        require(end<=len(data),'Truncated PNG chunk')
        require(zlib.crc32(data[at+4:at+8+length])&0xffffffff==struct.unpack_from('>I',data,at+8+length)[0],'PNG CRC mismatch')
        if kind==b'IHDR':
            require(size is None and length==13,'Malformed PNG header');size=struct.unpack_from('>II',data,at+8)
        at=end
        if kind==b'IEND': ended=True;break
    require(size and ended and at==len(data) and min(size)>1,'Incomplete PNG')
    return size

def validate_capture(base,before,after):
    meta=json.loads(base.with_suffix('.json').read_text(encoding='utf-8'))
    require(after['pid']==before['pid'] and after['sessionRunning'] and after['sessionState']=='FOCUSED','Capture lost session ownership/focus')
    require(after['captureSeq']>before['captureSeq'] and after['frame']>before['frame'],'Capture/frame counters did not advance')
    require(meta['name']==base.name and before['frame']<=meta['frameIndex']<=after['frame'],'Stale/misattributed capture sidecar')
    require(meta['layerCount']>0 and meta['sessionState']=='FOCUSED','Capture contains no focused composition')
    for eye in ('L','R'):
        value=meta['stats']['nonBlackPct'+eye]
        require(isinstance(value,(int,float)) and math.isfinite(value) and value>.01,'Empty/black eye capture')
    files={}
    for suffix,factor in (('left',1),('right',1),('sbs',2)):
        path=base.parent/(base.name+'_'+suffix+'.png')
        require(png_size(path)==(meta['width']*factor,meta['height']),'Eye PNG/sidecar dimensions disagree')
        files[suffix]=dict(path=path.name,sha256=digest(path))
    return dict(frame=meta['frameIndex'],displayTimeNs=meta['displayTimeNs'],width=meta['width'],height=meta['height'],layers=meta['layerCount'],files=files,
                json=base.with_suffix('.json').name,visualReview='PENDING')

@contextmanager
def lease(directory):
    path=Path(directory)/'.preyvr-regression.lock';token=uuid.uuid4().hex
    try:
        with path.open('x',encoding='ascii') as out: json.dump(dict(token=token,runnerPid=os.getpid()),out)
    except FileExistsError: raise Blocked(f'Another run or unrecovered lock owns {path}')
    try: yield
    finally:
        if path.exists() and json.loads(path.read_text(encoding='ascii')).get('token')==token: path.unlink()

class Runner:
    def __init__(self,backend,f,out,selected,label='candidate',expected_dll='',clock=time.monotonic,sleep=time.sleep):
        self.b=backend;self.f=f;self.out=Path(out);self.selected=selected;self.clock=clock;self.sleep=sleep
        self.expected_dll=expected_dll;self.pid=0;self.sequence=0;self.saved=None;self.initial=None;self.changed={};self.capture_index=0
        self.report=dict(schema=1,environment=backend.environment,label=label,fixture=f,fixtureHash=hashlib.sha256(json.dumps(f,sort_keys=True).encode()).hexdigest(),
                         driver=dict(runnerSha256=digest(__file__),bridgeSha256=digest(HERE/'bridge.ps1')),
                         verdict='RUNNING',visualReview='PENDING',scenarios=[],cleanup=[],events=[])
        self.active=None;self.drove_inputs=False
    def event(self,kind,**data): self.report['events'].append(dict(kind=kind,**data))
    def sample(self):
        data=self.b.call('sample');p=data['probe']
        require(p['schema']==1 and p['result']==0 and p['request']==data['request'] and p['pid']==self.pid,'Native response correlation/PID mismatch')
        require(p['sample']>self.sequence,'Native probe sequence did not advance');self.sequence=p['sample']
        self.event('state',probe=p)
        require(not p['grab']['fault'],'Surface lane is faulted')
        return p
    def wait(self,label,predicate,timeout=15):
        end=self.clock()+timeout;last=None
        while self.clock()<end:
            last=self.sample()
            if predicate(last): return last
            self.sleep(.05)
        raise Timeout(f'Timed out waiting for {label}; last sample {last["sample"] if last else "none"}')
    def need(self,label,predicate):
        try:return self.wait(label,predicate)
        except Timeout as error:raise Blocked(str(error)) from error
    @staticmethod
    def gameplay(p): return p['menu']['known'] and p['menu']['gameplay'] and p['menu']['mask']==0 and not p['menu']['options'] and p['tracking']['valid'] and p['pose']['valid']
    def sim(self,*lines): self.drove_inputs=True;self.event('sim',lines=lines);return self.b.call('sim',lines=list(lines))
    def mod(self,command):
        require(re.fullmatch(r'(?:aim.twohand|flight.controls|flight.grab|flight.grab.reach|flight.push.strength|flight.push.limit|ui.inventory|body.yaw|body.room) \d+',command),'Unsupported regression setting command')
        reply=self.b.call('mod',command=command)
        result_lines='\n'.join(line for line in reply['reply'].splitlines() if not line.lstrip().startswith('{'))
        require(re.search(r'\bresult=0\b',result_lines) and not re.search(r'\bresult=(?!0\b)\S+',result_lines),'Native setter refused or omitted result: '+reply['reply'])
        require(reply['probe']['pid']==self.pid and reply['probe']['request']==reply['request'],'Setter reply correlation mismatch')
        self.event('mod',command=command,reply=reply['reply']);return reply['probe']
    def setting(self,key,command,value):
        if key not in self.changed: self.changed[key]=(command,self.saved[key])
        self.mod(f'{command} {int(value)}')
    def shot(self,name):
        p=self.wait('fresh state before capture',lambda p:p['menu']['known'] and p['tracking']['valid'])
        before=self.b.call('status')['state'];self.capture_index+=1
        tag=f'r{self.report["runId"]}-{self.capture_index:02d}-{name}'
        base=self.out/tag
        self.b.call('capture',outBase=str(base))
        after=self.b.call('status')['state'];cap=validate_capture(base,before,after);q=self.sample()
        require(q['tracking']['valid'] and p['tracking']['epoch']==q['tracking']['epoch'] and p['reference']==q['reference'],'Capture crossed tracking/reference context')
        require(p['tracking']['displayTimeNs']<=cap['displayTimeNs']<=q['tracking']['displayTimeNs'],'Capture is outside observed XR publication interval')
        require(q['menu']['known'] and p['menu']['epoch']==q['menu']['epoch'] and p['menu']['mask']==q['menu']['mask'],'Native modal owner changed during capture')
        require(p['twoHand']['held']==q['twoHand']['held'] and p['grab']['owns']==q['grab']['owns'],'Interaction ownership changed during capture')
        if p['pose']['valid'] and q['pose']['valid']:
            require(p['pose']['player']==q['pose']['player'] and p['pose']['weapon']==q['pose']['weapon'],'Player/weapon changed during capture')
        cap.update(name=name,probeBefore=p,probeAfter=q)
        self.active['captures'].append(cap);self.event('capture',name=name,frame=cap['frame']);self.save()
        return p
    def rig(self,left=None):
        lines=['input clear','hand l follow off','hand r follow off',f'head pos {text(self.f["head"])}',f'head rot {text(self.f["headYpr"])}']
        for hand,point in (('l',left if left is not None else self.f['left']),('r',self.f['right'])):
            for pose in ('grip','aim'): lines.append(f'hand {hand} {pose} pos {text(point)}')
            for pose in ('grip','aim'): lines.append(f'hand {hand} {pose} rot {text(self.f["rightYpr"])}')
        self.sim(*lines)
        expected_left=left if left is not None else self.f['left']
        self.wait('configured neutral rig in matching simulator/client coordinates (use a fresh xr-sim origin)',lambda p:p['tracking']['valid'] and distance(p['tracking']['head'],self.f['head'])<.003 and
                  distance(p['tracking']['left'],expected_left)<.003 and distance(p['tracking']['right'],self.f['right'])<.003 and
                  not p['tracking']['leftGrip'] and not p['tracking']['rightGrip'])
    def menu(self):
        require(self.sample()['menu']['navigationEnabled'],'Menu navigation is disabled; enable VR with normal startup first')
        self.sim('btn menu press 150');self.wait('native pause menu',lambda p:p['menu']['known'] and p['menu']['mask']==1)
        p=self.shot('menu-before');actions=p['menu']['actions']
        self.b.call('pulse',lines=['stick r 0 -1'],releaseLines=['stick r center'],durationMs=150)
        self.wait('menu navigation dispatch',lambda p:p['menu']['actions']>actions)
        self.wait('pause menu remains owner',lambda p:p['menu']['mask']==1);self.shot('menu-after-down')
        self.b.call('pulse',lines=['stick r 0 1'],releaseLines=['stick r center'],durationMs=150)
        self.wait('reverse menu navigation dispatch',lambda p:p['menu']['actions']>actions+1);self.shot('menu-after-up')
        self.sim('btn b press 150');self.wait('gameplay after pause',self.gameplay);self.shot('menu-closed')
        self.active['limits']='Dispatch and native modal ownership checked; selected row requires image review.'
    def inventory(self):
        self.setting('inventory','ui.inventory',1);before=self.shot('inventory-before')
        self.sim('btn x press 150');p=self.wait('native PDA visible and redirected frames advancing',lambda p:p['menu']['inventory'] and p['inventoryFrames']>before['inventoryFrames'])
        self.shot('inventory-open');actions=p['menu']['actions']
        self.sim('grip r 1');self.wait('right grip observed',lambda p:p['tracking']['rightGrip']);self.sim('grip r 0')
        self.wait('inventory tab navigation dispatch',lambda p:p['menu']['inventory'] and p['menu']['actions']>actions);self.shot('inventory-after-tab')
        self.sim('btn b press 150');self.wait('gameplay after PDA',self.gameplay);self.shot('inventory-closed')
        self.active['limits']='Native PDA visibility, separate-layer frame delivery and tab dispatch checked; tab contents require image review.'
    def two_hand(self):
        self.setting('twoHand','aim.twohand',1)
        p=self.need('current native long-weapon foregrip hint',lambda p:self.gameplay(p) and p['twoHand']['target'] is not None)
        target=vector(p['twoHand']['target']);self.sim(f'hand l grip pos {text(target)}',f'hand l aim pos {text(target)}')
        self.wait('left hand inside native foregrip',lambda p:p['twoHand']['near'] and not p['twoHand']['held']);self.shot('two-hand-before')
        self.sim('grip l 1');self.wait('two-hand solver attached with independent hands',lambda p:p['twoHand']['held'] and p['twoHand']['blend']>=.95 and p['tracking']['leftGrip'] and not p['tracking']['rightGrip']);self.shot('two-hand-attached')
        self.sim('grip l 0')
        if self.saved['toggle']:
            self.wait('release before toggle detach',lambda p:not p['tracking']['leftGrip']);self.sim('grip l 1')
            self.wait('toggle detaches',lambda p:not p['twoHand']['held']);self.sim('grip l 0')
        self.wait('two-hand solver detached',lambda p:not p['twoHand']['held'] and p['twoHand']['blend']<.05);self.shot('two-hand-detached')
        self.active['limits']='Solver ownership and measured region checked; visual hand/weapon alignment requires image review.'
    def anchored(self):
        self.sim('grip l 1')
        return self.wait('fixed surface acquired and native submission observed',lambda p:self.gameplay(p) and p['grab']['owns'] and p['grab']['submitted']>self.anchor_base and p['tracking']['leftGrip'] and not p['tracking']['rightGrip'])
    def stable_anchor(self):
        previous=self.sample();start=self.clock();end=start+8
        while self.clock()<end:
            current=self.wait('anchor remains owned',lambda p:self.gameplay(p) and p['grab']['owns'])
            if self.clock()-start>=.1:
                if distance(previous['pose']['camera'],current['pose']['camera'])<.002 and current['grab']['submitted']>previous['grab']['submitted']:return current
                previous=current;start=self.clock()
            self.sleep(.05)
        raise Failure('Anchored camera did not stabilize; existing drift cannot be used as evidence of physical pull')
    def check_motion(self,a,b,stroke):
        require(self.gameplay(b) and a['pose']['player']==b['pose']['player'] and a['pose']['weapon']==b['pose']['weapon'] and a['reference']==b['reference'],'Motion crossed player/weapon/reference/menu context')
        require(distance(a['tracking']['head'],b['tracking']['head'])<.003,'Simulated head moved during physical stroke')
        require(distance(a['tracking']['rotation'],b['tracking']['rotation'])<.003,'Simulated head rotated during physical stroke')
        delta=[y-x for x,y in zip(a['pose']['camera'],b['pose']['camera'])]
        local=[-stroke[0],stroke[2],-stroke[1]];yaw=a['pose']['yaw']
        expected=[math.cos(yaw)*local[0]-math.sin(yaw)*local[1],math.sin(yaw)*local[0]+math.cos(yaw)*local[1],local[2]]
        require(distance(delta,[0,0,0])>=self.f['zeroG']['minCameraMotion'] and sum(x*y for x,y in zip(delta,expected))>0,'No camera movement in expected physical direction with stationary tracked head')
        self.active.setdefault('motion',[]).append(dict(cameraDelta=delta,expectedDirection=expected))
    def zero_g(self):
        self.setting('twoHand','aim.twohand',0);self.setting('flight','flight.controls',1);self.setting('grab','flight.grab',1)
        self.setting('reach','flight.grab.reach',10);self.setting('strength','flight.push.strength',100);self.setting('limit','flight.push.limit',150)
        z=self.f['zeroG'];self.rig(z['leftAtWall'])
        p=self.need('ordinary native zero-g state9',lambda p:self.gameplay(p) and p['flight']['active'] and p['flight']['ownerError']==0 and p['flight']['state']==9)
        self.shot('zero-g-before');self.anchor_base=p['grab']['submitted'];self.anchored();self.stable_anchor();a=self.shot('zero-g-anchored')
        self.b.call('motion',start=z['leftAtWall'],delta=z['stroke'],durationMs=z['durationMs'],release=False)
        b=self.wait('pull submission and current pose',lambda p:self.gameplay(p) and p['grab']['owns'] and p['grab']['submitted']>a['grab']['submitted'])
        self.check_motion(a,b,z['stroke']);self.shot('zero-g-after-pull')
        # Still release control: holding before the command/capture roundtrip
        # lets the physical velocity estimator decay, so no push is authorized.
        p=self.sample();self.sim('grip l 0');p=self.wait('still release without launch',lambda s:not s['grab']['owns'])
        require(p['grab']['pushes']==a['grab']['pushes'],'Stationary release unexpectedly authorized a push')
        hand=add(z['leftAtWall'],z['stroke']);self.anchor_base=p['grab']['submitted'];self.anchored();self.stable_anchor();a=self.shot('zero-g-before-push')
        self.b.call('motion',start=hand,delta=z['stroke'],durationMs=z['durationMs'],release=True)
        b=self.wait('one physical push and acknowledged native release delivery',lambda p:self.gameplay(p) and p['grab']['pushes']==a['grab']['pushes']+1 and
                    p['grab']['releaseSerial']>a['grab']['releaseSerial'] and p['grab']['submitted']>a['grab']['submitted'] and not p['grab']['owns'])
        self.check_motion(a,b,z['stroke']);self.shot('zero-g-after-push');p=self.sample()
        require(p['grab']['pushes']==a['grab']['pushes']+1 and p['grab']['releaseSerial']==b['grab']['releaseSerial'],'Release command repeated')
        # Return the free virtual hand to the same world anchor after drifting
        # away. Move only the controller, never the player or the game camera.
        offset=[x-y for x,y in zip(a['pose']['camera'],p['pose']['camera'])];yaw=p['pose']['yaw']
        local=[math.cos(yaw)*offset[0]+math.sin(yaw)*offset[1],-math.sin(yaw)*offset[0]+math.cos(yaw)*offset[1],offset[2]]
        at_wall=vector(add(a['tracking']['left'],[local[0],local[2],-local[1]]))
        self.sim(f'hand l grip pos {text(at_wall)}',f'hand l aim pos {text(at_wall)}')
        self.anchor_base=p['grab']['submitted'];self.anchored();a=self.sample();self.sim('btn menu press 150')
        b=self.wait('menu cancels surface owner',lambda p:p['menu']['mask']==1 and not p['grab']['owns'])
        require(b['grab']['pushes']==a['grab']['pushes'],'Menu cancellation authorized a launch')
        self.sim('grip l 0');self.shot('zero-g-menu-cancelled');self.sim('btn b press 150');self.wait('gameplay after cancellation',self.gameplay)
        self.active['limits']='Fixed-contact acquisition, native submission and camera movement with stationary XR head checked. Collision coverage, velocity peak and headset feel remain separate acceptance checks.'
    def save(self):
        dump(self.out/'report.json',self.report)
        rows=[]
        for case in self.report['scenarios']:
            images=''.join('<figure><figcaption>'+html.escape(cap['name'])+'</figcaption>'+''.join(
                f'<a href="{html.escape(cap["files"][eye]["path"],quote=True)}"><img src="{html.escape(cap["files"][eye]["path"],quote=True)}" alt="{eye} eye"></a>' for eye in ('left','right'))+'</figure>' for cap in case['captures'])
            rows.append('<section><h2>'+html.escape(case['name'])+' — '+html.escape(case['verdict'])+'</h2><p>'+html.escape(case.get('error',case.get('limits','')))+ '</p>'+images+'</section>')
        page='<!doctype html><meta charset="utf-8"><title>PreyVR regression</title><style>body{background:#0c1218;color:#e8e6d5;font:16px system-ui;margin:32px}h1,h2{color:#f1a83f}figure{display:inline-block;width:46%;vertical-align:top}img{width:49%}figcaption{margin:12px 0}a{color:#f1a83f}</style><h1>PreyVR regression — '+html.escape(self.report['verdict'])+'</h1><p>Environment: '+html.escape(self.report['environment'])+'. Visual review: PENDING. Each pair shows left and right eyes.</p>'+''.join(rows)
        (self.out/'report.html').write_text(page,encoding='utf-8')
    def run(self):
        require(not self.out.exists(),'Output directory already exists; use a unique run directory');self.out.mkdir(parents=True)
        self.report['runId']=uuid.uuid4().hex[:12]
        try:
            info=self.b.call('preflight');self.pid=info['ownerPid'];self.initial=info['state'];self.report['identity']=info
            require(info['gameHash']==TARGET,'Unsupported game identity')
            require(not self.expected_dll or info['loadedDllHash']==self.expected_dll,'Injected DLL differs from expected build')
            before=self.b.call('status')['state'];self.sleep(.1);after=self.b.call('status')['state']
            require(after['pid']==self.pid and after['frame']>before['frame'],'xr-sim session is stale or frames stalled')
            self.saved=self.sample()['settings'].copy()
            self.need('fresh gameplay at prepared fixture',self.gameplay)
            # Freeze body-follow policy during motion checks; restore it later.
            self.setting('bodyYaw','body.yaw',0);self.setting('bodyRoom','body.room',0)
            for name in self.selected:
                print('Scenario:',name,flush=True);self.active=dict(name=name,verdict='RUNNING',captures=[]);self.report['scenarios'].append(self.active)
                self.rig();self.wait('gameplay before '+name,self.gameplay);getattr(self,name)();self.active['verdict']='CHECKS_PASSED';self.save()
            self.report['verdict']='CHECKS_PASSED_REVIEW_PENDING'
        except (Exception,KeyboardInterrupt) as error:
            self.report['verdict']='BLOCKED' if isinstance(error,Blocked) else 'FAILED';self.report['error']=str(error)
            if self.active:self.active.update(verdict=self.report['verdict'],error=str(error))
        finally:
            if self.saved is not None and (self.drove_inputs or self.changed):
                safe_release=True
                if 'grab' in self.changed:
                    try:self.mod('flight.grab 0') # revoke release authority before dropping a moving grip
                    except Exception as error:
                        safe_release=False;self.report['cleanup'].append('Cannot disarm surface release; inputs left held for recovery: '+str(error))
                if safe_release:
                    try:
                        self.sim('input clear');p=self.sample()
                        if p['menu']['known'] and p['menu']['mask'] in (1,16):self.sim('btn b press 150');self.wait('owned menu closed during cleanup',self.gameplay)
                    except Exception as error:self.report['cleanup'].append('Input/menu cleanup failed: '+str(error))
                for key,(command,value) in reversed(list(self.changed.items())):
                    try:self.mod(f'{command} {int(value)}')
                    except Exception as error:self.report['cleanup'].append(f'{key} restoration failed: {error}')
                try:
                    current=self.sample()['settings']
                    require(all(current[k]==v for k,(_,v) in self.changed.items()),'Runtime preference readback mismatch')
                except Exception as error:self.report['cleanup'].append(str(error))
            if self.report['cleanup']:self.report['verdict']='FAILED_CLEANUP'
            self.save()
        return self.report

def compare(report,baseline_file,out):
    base_path=Path(baseline_file);base=json.loads(base_path.read_text(encoding='utf-8'))
    require(base['schema']==1 and base['verdict']=='CHECKS_PASSED_REVIEW_PENDING','Baseline did not finish its automated checks')
    require(base['environment']==report['environment'] and base['fixtureHash']==report['fixtureHash'],'Baseline fixture/environment mismatch')
    require(base['identity']['gameHash']==report['identity']['gameHash'],'Baseline target mismatch')
    for field in ('ipdMm','fovDeg','recommendedViews'):
        require(base['identity']['state'].get(field)==report['identity']['state'].get(field),'Baseline optics/profile mismatch: '+field)
    a={c['name']:c for case in base['scenarios'] for c in case['captures']};b={c['name']:c for case in report['scenarios'] for c in case['captures']}
    require(b.keys()<=a.keys(),'Candidate has captures absent from baseline')
    missing=sorted(a.keys()-b.keys())
    if report['verdict']=='CHECKS_PASSED_REVIEW_PENDING':require(not missing,'Baseline scenario/capture coverage mismatch')
    rows=[]
    for name,cap in b.items():
        require(re.fullmatch(r'[a-z0-9_-]{1,80}',name),'Invalid capture label')
        old=a[name];require((old['width'],old['height'])==(cap['width'],cap['height']),'Baseline capture size mismatch')
        entry=dict(name=name,eyes={})
        for eye in ('left','right'):
            path=(base_path.parent/old['files'][eye]['path']).resolve()
            require(path.parent==base_path.parent.resolve() and digest(path)==old['files'][eye]['sha256'],'Baseline artifact missing, changed or outside report directory')
            target=Path(out)/'baseline'/f'{name}_{eye}.png';target.parent.mkdir(exist_ok=True)
            shutil.copyfile(path,target)
            entry['eyes'][eye]=dict(baseline=target.relative_to(out).as_posix(),candidate=cap['files'][eye]['path'],unchanged=old['files'][eye]['sha256']==cap['files'][eye]['sha256'])
        rows.append(entry)
    dump(Path(out)/'comparison.json',dict(visualReview='PENDING',candidateVerdict=report['verdict'],missingCaptures=missing,captures=rows))
    blocks=[]
    for row in rows:
        images=''.join(f'<h3>{eye}</h3><img src="{html.escape(e["baseline"],quote=True)}"><img src="{html.escape(e["candidate"],quote=True)}">' for eye,e in row['eyes'].items())
        blocks.append('<section><h2>'+html.escape(row['name'])+'</h2>'+images+'</section>')
    (Path(out)/'comparison.html').write_text('<!doctype html><meta charset="utf-8"><title>PreyVR before / after</title><style>body{background:#0c1218;color:#e8e6d5;font:16px system-ui;margin:30px}img{width:46%}</style><h1>Baseline / candidate — review pending</h1><p>Candidate: '+html.escape(report['verdict'])+'. Missing captures: '+html.escape(', '.join(missing) or 'none')+'. Pixel/hash differences describe change, not correctness.</p>'+''.join(blocks),encoding='utf-8')

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--fixture',type=Path,default=HERE/'default-fixture.json')
    p.add_argument('--scenarios',default=','.join(SCENARIOS));p.add_argument('--label',default='candidate');p.add_argument('--baseline',type=Path)
    for name in ('run-dir','state-dir','dll','out','sim-root'):p.add_argument('--'+name,default='')
    p.add_argument('--powershell',default='pwsh');args=p.parse_args(argv)
    try:
        f=fixture(args.fixture);selected=args.scenarios.split(',')
        require(selected and len(set(selected))==len(selected) and all(s in SCENARIOS for s in selected),'Unknown or duplicate scenario')
        if not args.execute:print(json.dumps(plan(f,selected),indent=2));return 0
        require(all((args.run_dir,args.state_dir,args.dll,args.out,args.sim_root)),'Execution needs explicit run/state directories, DLL, output and xr-sim root')
        dll=Path(args.dll).resolve();require(dll.is_file(),'Expected DLL does not exist')
        out=Path(args.out).resolve();require(not out.exists(),'Use a new output directory')
        with lease(args.state_dir):
            backend=WindowsBackend(args,out);runner=Runner(backend,f,out,selected,args.label,digest(dll));r=runner.run()
        if args.baseline and 'identity' in r:
            try:compare(r,args.baseline,out)
            except (Failure,OSError,ValueError,KeyError) as error:
                r['comparisonError']=str(error)
                if r['verdict']=='CHECKS_PASSED_REVIEW_PENDING':r['verdict']='FAILED_COMPARISON'
                runner.save()
        print(r['verdict']+' — '+str(out/'report.html'))
        return 0 if r['verdict']=='CHECKS_PASSED_REVIEW_PENDING' else 1
    except (Failure,ValueError,OSError,KeyError) as error:print('BLOCKED: '+str(error));return 2

if __name__=='__main__':raise SystemExit(main())
