"""Exercise every raw channel and deterministic mixed full-body poses in native UE."""
import json,math,random
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());fixtures=root/'Shared/Fixtures'
profile=json.loads((root/'Shared/Profiles/virtual_humanoid_44_v1.json').read_text(encoding='utf-8'))
axes={a['id']:a for a in profile['axes']};order=profile['axis_order']
base=json.loads((fixtures/'neutral.sample.json').read_text(encoding='utf-8'))
cases=[];report={'passed':False,'channel_count':44,'failures':[],'maximum_bone_length_delta_cm':0.}
def call(a,b=''):return json.loads(unreal.PoseDollEditorLibrary.session_command(a,b))
call('mask','FullBody')
for chain in ('leg_l','leg_r','arm_l','arm_r'):call('contact',json.dumps({'chain':chain,'enabled':False}))
assert call('fixture','neutral.sample.json')['ok']
bones=call('pose_report')['bones']
pairs=[(f'{a}_{s}',f'{b}_{s}') for s in ('l','r') for a,b in (('upperarm','lowerarm'),('lowerarm','hand'),('thigh','calf'),('calf','foot'))]
lengths={(a,b):math.dist(bones[a]['translation_cm'],bones[b]['translation_cm']) for a,b in pairs}
for aid in order:
    lo,hi=axes[aid]['limits_rad']
    for f in (0.,.25,.5,.75,1.):
        q=dict.fromkeys(order,0.);q[aid]=lo+(hi-lo)*f;cases.append((aid+'@'+str(f),q))
rng=random.Random(44154)
for i in range(40):cases.append((f'mixed_{i}',{a:rng.uniform(*axes[a]['limits_rad']) for a in order}))
try:
    for name,q in cases:
        sample=json.loads(json.dumps(base));sample['raw_angles_rad']=[(math.pi+q[a])%(2*math.pi) for a in order]
        (fixtures/'axis_sweep.sample.json').write_text(json.dumps(sample),encoding='utf-8')
        r=call('fixture','axis_sweep.sample.json')
        if not r['ok']:report['failures'].append({'case':name,'error':r['error'],'q':q});continue
        actual=call('pose_report')['bones']
        err=max(abs(math.dist(actual[a]['translation_cm'],actual[b]['translation_cm'])-lengths[a,b]) for a,b in pairs)
        report['maximum_bone_length_delta_cm']=max(report['maximum_bone_length_delta_cm'],err)
        if err>.001:report['failures'].append({'case':name,'length_error_cm':err})
    report['case_count']=len(cases);report['passed']=not report['failures']
    assert report['passed'],report['failures'][:2]
    unreal.log('POSEDOLL_44_AXIS_SWEEP_SUCCESS')
finally:
    (root/'reports/axis_sweep.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
