"""Offline runner tests. Fake native/runtime boundaries; no Prey/XR launch."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('regression_runner',ROOT/'tools/regression/runner.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)

def png(path,w,h,color):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raw=b''.join(b'\0'+bytes(color)*w for _ in range(h))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))

class Clock:
    def __init__(self):self.now=0
    def time(self):return self.now
    def sleep(self,n):self.now+=n

class FakeBackend:
    environment='harness'
    def __init__(self,f,clock):
        self.f=f;self.clock=clock;self.calls=[];self.frame=1;self.seq=0;self.actions=0;self.mask=0;self.inventory=0
        self.head=f['head'][:];self.left=f['left'][:];self.right=f['right'][:];self.left_grip=self.right_grip=False
        self.near=False;self.held=False;self.target=[.1,1.35,-.55];self.camera=[0,0,0]
        self.settings=dict(twoHand=1,toggle=False,flight=0,grab=0,reach=10,strength=100,limit=150,inventory=False,bodyYaw=1,bodyRoom=1)
        self.grab=dict(owns=False,fault=False,ownerError=0,captures=0,pushes=0,submitted=0,releaseSerial=0,cancelled=0,refused=0)
        self.state=dict(pid=123,runtime='xr-sim',graphics='D3D11',sessionRunning=True,sessionState='FOCUSED',frame=1,captureSeq=0,
                        ipdMm=63,fovDeg=dict(l=-54,r=44,u=55,d=-55),recommendedViews=[dict(width=4,height=4)])
        self.identity=dict(ownerPid=123,ownerStart='456',gameHash=r.TARGET,loadedDllHash='a'*64,module='fake.dll')
        self.fault='';self.last_command='';self.restore_fail=False
    def tick(self):
        self.clock.now+=.02
        if self.fault!='stalled':self.frame+=10
        self.state['frame']=self.frame
        if self.grab['owns']:self.grab['submitted']+=1
    def probe(self):
        if self.fault!='probe-stalled':self.seq+=1
        if self.mask==16 and self.settings['inventory']:self.inventory+=5
        return dict(schema=1,result=0,request='test',pid=999 if self.fault=='wrong-pid' else 123,sample=self.seq,reference=2,module='fake.dll',
            menu=dict(known=True,mask=self.mask,epoch=1,samples=self.seq,inventory=self.mask==16,gameplay=self.mask==0,options=False,navigationEnabled=True,actions=self.actions),
            tracking=dict(valid=True,sequence=self.seq,epoch=1,displayTimeNs=self.frame*1000000,head=self.head[:],rotation=[0,0,0,1],left=self.left[:],right=self.right[:],leftGrip=self.left_grip,rightGrip=self.right_grip),
            pose=dict(valid=True,player=10,weapon=20,camera=self.camera[:],yaw=0),
            twoHand=dict(held=self.held,near=self.near,blend=1 if self.held else 0,target=None if self.fault=='no-region' else self.target[:]),
            flight=dict(active=True,state=0 if self.fault=='ground' else 9,ownerError=0),grab=copy.deepcopy(self.grab),inventoryFrames=self.inventory,settings=copy.deepcopy(self.settings))
    def line(self,line):
        p=line.split()
        if line.startswith('head pos '):self.head=list(map(float,p[2:]))
        if p[:2]==['hand','l'] and p[2:4] in (['grip','pos'],['aim','pos']):self.left=list(map(float,p[4:]));self.near=r.distance(self.left,self.target)<.01
        if p[:2]==['hand','r'] and p[2:4] in (['grip','pos'],['aim','pos']):self.right=list(map(float,p[4:]))
        if line=='input clear':self.left_grip=self.right_grip=False;self.grab['owns']=False;self.held=False
        if p[:2]==['grip','r']:
            old=self.right_grip;self.right_grip=float(p[2])>0
            if old and not self.right_grip and self.mask==16:self.actions+=1
        if p[:2]==['grip','l']:
            was=self.left_grip;self.left_grip=float(p[2])>0
            if self.fault=='aliased':self.right_grip=self.left_grip
            if self.left_grip and not was and self.near and self.settings['twoHand']:
                self.held=not self.held if self.settings['toggle'] else True
            elif not self.left_grip and not self.settings['toggle']:self.held=False
            if self.left_grip and not was and self.settings['grab'] and self.settings['flight'] and not self.mask and self.fault!='no-wall':
                self.grab['owns']=True;self.grab['captures']+=1
            if not self.left_grip:self.grab['owns']=False
        if p[:2]==['btn','menu']:
            self.mask=1;self.actions+=1
            if self.grab['owns']:self.grab['cancelled']+=1
            self.grab['owns']=False
        if p[:2]==['btn','b']:self.mask=0;self.actions+=1
        if p[:2]==['btn','x']:self.mask=16;self.actions+=1
        if p[:2]==['stick','r'] and p[2]!='center':self.actions+=1
    def call(self,action,**fields):
        self.tick();self.calls.append((action,fields))
        if action=='preflight':return dict(self.identity,state=copy.deepcopy(self.state))
        if action=='status':return dict(state=copy.deepcopy(self.state))
        if action in ('sample','mod'):
            reply='result=0'
            if action=='mod':
                line=fields['command'];self.last_command=line;command,value=line.split();value=int(value)
                key={'aim.twohand':'twoHand','flight.controls':'flight','flight.grab':'grab','flight.grab.reach':'reach',
                     'flight.push.strength':'strength','flight.push.limit':'limit','ui.inventory':'inventory','body.yaw':'bodyYaw','body.room':'bodyRoom'}[command]
                if self.restore_fail and key=='bodyYaw' and value==1:reply='result=5'
                else:self.settings[key]=value
            probe=self.probe()
            if self.fault=='wrong-nonce':probe['request']='stale'
            return dict(probe=probe,request='test',reply=reply)
        if action in ('sim','pulse'):
            for line in fields['lines']:self.line(line)
            if action=='pulse':
                for line in fields['releaseLines']:self.line(line)
            return dict(state=copy.deepcopy(self.state))
        if action=='motion':
            self.left=r.add(fields['start'],fields['delta'])
            if self.fault!='no-motion':self.camera=r.add(self.camera,[-fields['delta'][0],fields['delta'][2],-fields['delta'][1]])
            if fields['release']:
                self.left_grip=False;self.grab['owns']=False;self.grab['pushes']+=1;self.grab['submitted']+=1
                if self.fault!='no-release-delivery':self.grab['releaseSerial']+=1
            return dict(state=copy.deepcopy(self.state),steps=15,elapsedMs=200)
        if action=='capture':
            base=Path(fields['outBase']);self.state['captureSeq']+=1
            meta=dict(name=base.name,frameIndex=0 if self.fault=='stale-capture' else self.frame,displayTimeNs=self.frame*1000000,width=4,height=4,layerCount=2,
                      sessionState='FOCUSED',stats=dict(nonBlackPctL=100,nonBlackPctR=0 if self.fault=='black-eye' else 100))
            r.dump(base.with_suffix('.json'),meta)
            if self.fault=='capture-modal-transition':self.mask=16
            for suffix,w in (('left',4),('right',4),('sbs',8)):png(base.parent/(base.name+'_'+suffix+'.png'),w,4,[20,30,40,255])
            return dict(state=copy.deepcopy(self.state))
        raise AssertionError('Unexpected backend action '+action)

class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='preyvr-offline-regression-');self.root=Path(self.temp.name)
        self.f=r.fixture(ROOT/'tools/regression/default-fixture.json');self.clock=Clock();self.b=FakeBackend(self.f,self.clock)
    def tearDown(self):self.temp.cleanup()
    def run_case(self,selected=r.SCENARIOS,name='run'):
        runner=r.Runner(self.b,self.f,self.root/name,selected,expected_dll='a'*64,clock=self.clock.time,sleep=self.clock.sleep)
        with mock.patch('sys.stdout',new=io.StringIO()):report=runner.run()
        return report
    def test_plan_has_no_backend_or_live_command(self):
        with mock.patch.object(r,'WindowsBackend',side_effect=AssertionError('plan touched backend')),mock.patch('sys.stdout',new=io.StringIO()) as out:
            self.assertEqual(r.main([]),0)
            p=json.loads(out.getvalue());self.assertFalse(p['writes_commands']);self.assertEqual(len(p['scenarios']),4)
    def test_whole_flow_captures_both_eyes_and_restores_settings(self):
        saved=self.b.settings.copy();report=self.run_case()
        self.assertEqual(report['verdict'],'CHECKS_PASSED_REVIEW_PENDING',report.get('error'))
        self.assertEqual(self.b.settings,saved);self.assertFalse(self.b.left_grip);self.assertFalse(self.b.right_grip)
        self.assertEqual(len(report['scenarios']),4);self.assertEqual(report['environment'],'harness')
        self.assertEqual(sum(len(x['captures']) for x in report['scenarios']),17)
        self.assertTrue((self.root/'run/report.html').is_file());self.assertEqual(report['visualReview'],'PENDING')
        self.assertEqual(self.b.grab['pushes'],1)
    def test_toggle_grip_detaches_with_second_squeeze(self):
        self.b.settings['toggle']=True
        self.assertEqual(self.run_case(['two_hand'])['verdict'],'CHECKS_PASSED_REVIEW_PENDING')
        self.assertFalse(self.b.held)
    def test_dead_session_or_wrong_identity_never_drives_input(self):
        for fault in ('stalled','wrong-pid','wrong-nonce'):
            with self.subTest(fault=fault):
                self.b=FakeBackend(self.f,self.clock);self.b.fault=fault
                self.assertNotEqual(self.run_case(['menu'],fault)['verdict'],'CHECKS_PASSED_REVIEW_PENDING')
                self.assertFalse(any(action in ('sim','pulse','motion') for action,_ in self.b.calls))
    def test_unprepared_initial_menu_is_blocked_without_closing_it(self):
        self.b.mask=16;report=self.run_case(['menu'])
        self.assertEqual(report['verdict'],'BLOCKED');self.assertEqual(self.b.mask,16)
        self.assertFalse(any(action in ('sim','pulse','motion','mod') for action,_ in self.b.calls))
    def test_empty_stale_or_black_capture_is_failure(self):
        for fault in ('stale-capture','black-eye','capture-modal-transition'):
            with self.subTest(fault=fault):
                self.b=FakeBackend(self.f,self.clock);self.b.fault=fault
                self.assertNotEqual(self.run_case(['menu'],fault)['verdict'],'CHECKS_PASSED_REVIEW_PENDING')
    def test_counts_alone_cannot_pass_zero_g_motion(self):
        self.b.fault='no-motion';report=self.run_case(['zero_g'])
        self.assertNotEqual(report['verdict'],'CHECKS_PASSED_REVIEW_PENDING');self.assertIn('camera movement',report['error'])
        self.assertEqual(self.b.settings['grab'],0)
    def test_held_submissions_cannot_stand_in_for_delivered_push(self):
        self.b.fault='no-release-delivery';report=self.run_case(['zero_g'])
        self.assertNotEqual(report['verdict'],'CHECKS_PASSED_REVIEW_PENDING')
        self.assertIn('release delivery',report['error']);self.assertEqual(self.b.grab['pushes'],1);self.assertEqual(self.b.grab['releaseSerial'],0)
    def test_no_region_wall_or_native_flight_cannot_pass(self):
        for fault,scenario in (('no-region','two_hand'),('aliased','two_hand'),('no-wall','zero_g'),('ground','zero_g')):
            with self.subTest(fault=fault):
                self.b=FakeBackend(self.f,self.clock);self.b.fault=fault
                self.assertNotEqual(self.run_case([scenario],fault)['verdict'],'CHECKS_PASSED_REVIEW_PENDING')
    def test_cleanup_failure_overrides_green_scenarios(self):
        self.b.restore_fail=True;report=self.run_case(['menu'])
        self.assertEqual(report['verdict'],'FAILED_CLEANUP');self.assertTrue(report['cleanup'])
    def test_cleanup_disarms_push_authority_before_releasing_input(self):
        self.b.fault='no-motion';self.run_case(['zero_g'])
        indices=[n for n,(action,fields) in enumerate(self.b.calls) if action=='mod' and fields.get('command')=='flight.grab 0']
        self.assertTrue(indices)
        self.assertTrue(any(action=='sim' and fields['lines']==['input clear'] for action,fields in self.b.calls[indices[0]+1:]))
    def test_baseline_preserves_artifact_hashes_and_checks_fixture(self):
        baseline=self.run_case(['menu'],'before');self.b=FakeBackend(self.f,self.clock)
        after=self.run_case(['menu'],'after');r.compare(after,self.root/'before/report.json',self.root/'after')
        self.assertTrue((self.root/'after/comparison.html').is_file())
        changed=copy.deepcopy(after);changed['fixtureHash']='bad'
        with self.assertRaises(r.Failure):r.compare(changed,self.root/'before/report.json',self.root/'after')
        changed=copy.deepcopy(after);changed['identity']['state']['ipdMm']=0
        with self.assertRaises(r.Failure):r.compare(changed,self.root/'before/report.json',self.root/'after')
        cap=baseline['scenarios'][0]['captures'][0];(self.root/'before'/cap['files']['left']['path']).write_bytes(b'corrupt')
        with self.assertRaises(r.Failure):r.compare(after,self.root/'before/report.json',self.root/'after')
    def test_lease_does_not_overwrite_another_owner(self):
        with r.lease(self.root):
            with self.assertRaises(r.Blocked):
                with r.lease(self.root):pass
        self.assertFalse((self.root/'.preyvr-regression.lock').exists())
    def test_failed_candidate_still_gets_partial_before_after_review(self):
        self.run_case(['menu'],'before');self.b=FakeBackend(self.f,self.clock)
        candidate=self.run_case(['menu'],'after');candidate['verdict']='FAILED';candidate['scenarios'][0]['captures'].pop()
        r.compare(candidate,self.root/'before/report.json',self.root/'after')
        review=json.loads((self.root/'after/comparison.json').read_text())
        self.assertEqual(review['candidateVerdict'],'FAILED');self.assertTrue(review['missingCaptures'])
    def test_png_validator_rejects_truncation_and_bad_crc(self):
        p=self.root/'valid.png';png(p,4,4,[1,2,3,255]);self.assertEqual(r.png_size(p),(4,4));data=p.read_bytes()
        for broken in (data[:-1],data[:40]+b'broken'+data[46:]):
            p.write_bytes(broken)
            with self.assertRaises(r.Failure):r.png_size(p)
    def test_fixture_rejects_nonfinite_huge_motion_and_duplicate_scenarios(self):
        angles=copy.deepcopy(self.f);angles['headYpr']=[30,-15,10];valid=self.root/'angles.json';r.dump(valid,angles)
        self.assertEqual(r.fixture(valid)['headYpr'],[30,-15,10])
        for value in (float('nan'),2):
            f=copy.deepcopy(self.f);f['zeroG']['stroke']=[value,0,0];path=self.root/'bad.json';r.dump(path,f) if value==2 else path.write_text(json.dumps(f))
            with self.assertRaises(r.Failure):r.fixture(path)
        with mock.patch('sys.stdout',new=io.StringIO()):self.assertEqual(r.main(['--scenarios','menu,menu']),2)
    def test_powershell_entry_and_bridge_parse_offline(self):
        shell=shutil.which('pwsh')
        if not shell:self.skipTest('PowerShell not available')
        for path in (ROOT/'tools/Invoke-PreyVRRegression.ps1',ROOT/'tools/regression/bridge.ps1'):
            code="$e=$null;[System.Management.Automation.Language.Parser]::ParseFile($args[0],[ref]$null,[ref]$e)|Out-Null;if($e.Count){$e|Out-String|Write-Output;exit 1}"
            # -File avoids -Command's platform-specific argument quoting.
            script=self.root/'parse.ps1';script.write_text('param($Path)\n'+code.replace('$args[0]','$Path'),encoding='utf-8')
            result=subprocess.run([shell,'-NoProfile','-NonInteractive','-File',str(script),str(path)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([shell,'-NoProfile','-NonInteractive','-File',str(ROOT/'tools/Invoke-PreyVRRegression.ps1'),'-Python',sys.executable],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertEqual(json.loads(result.stdout)['mode'],'PLAN_ONLY')
        result=subprocess.run([shell,'-NoProfile','-NonInteractive','-File',str(ROOT/'tests/RegressionBridge.Tests.ps1')],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
