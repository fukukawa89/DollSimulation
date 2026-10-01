"""Key an explicitly bound O22 payload into a target Control Rig. No live stream."""
import json
from pathlib import Path
import unreal

def solve(payload,profile,allow_synthetic=False):
    data=json.loads(unreal.PoseDollEditorLibrary.solve_measured_pose22(str(payload),str(profile),allow_synthetic))
    if not data.get('ok'):raise ValueError(data.get('error',data))
    return data

def snapshot(rig):
    assert rig.execute('Forwards Solve')
    out={};h=rig.get_hierarchy()
    for key in h.get_all_keys():
        if key.type!=unreal.RigElementType.BONE:continue
        t=h.get_global_transform(key)
        out[str(key.name)]={'p':[t.translation.x,t.translation.y,t.translation.z],
          'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'s':[t.scale3d.x,t.scale3d.y,t.scale3d.z]}
    return out

def errors(expected,actual):
    import math
    pe,re=0,0
    for k,v in expected.items():
        a=actual[k];pe=max(pe,math.dist(v['p'],a['p']))
        qa,qb=v['q'],a['q'];dot=abs(sum(x*y for x,y in zip(qa,qb)))/math.sqrt(sum(x*x for x in qa)*sum(x*x for x in qb))
        re=max(re,math.degrees(2*math.acos(min(1,dot))))
    return {'position_cm':pe,'rotation_deg':re}
