"""Calibrate thumb/fingertip contacts on a constructed, transient Manny rig."""
import unreal,json,math,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir());report={'passed':False,'sides':{}}
cal=json.loads((ROOT/'Scripts/hand_preset_calibration.json').read_text())
def key(n,t=unreal.RigElementType.BONE):return unreal.RigElementKey(name=n,type=t)
def vec(v):return [v.x,v.y,v.z]
def sub(a,b):return [x-y for x,y in zip(a,b)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def length(v):return math.sqrt(dot(v,v))
def unit(v):return [x/length(v) for x in v]
try:
 asset=unreal.load_asset('/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body')
 for side in ('l','r'):
  rig=asset.create_control_rig();rig.request_construction();assert rig.execute('Construction');assert rig.execute('Forwards Solve');h=rig.get_hierarchy();out={}
  for fingers,states in [(('index',),'BBEEE'),(('middle',),'BEBEE'),(('index','middle'),'BBBEE')]:
   values={n:r[:] for n,r in cal['sides'][side]['controls'].items()}
   values['thumb_01_'+side+'_ctrl']=[32,10,-10];values['thumb_02_'+side+'_ctrl']=[0,0,-12];values['thumb_03_'+side+'_ctrl']=[0,0,-10]
   for f in fingers:
    for j,curl in enumerate((-25,-30,-15),1):values[f'{f}_{j:02}_{side}_ctrl']=[0,0,curl]
   if len(fingers)==2:
    # Bring index and middle finger pads together before opposing the thumb.
    values['index_01_'+side+'_ctrl'][1]+=8
    values['middle_01_'+side+'_ctrl'][1]-=5
   def evaluate():
    h.reset_pose_to_initial(unreal.RigElementType.ALL)
    for n,r in values.items():
     e=unreal.EulerTransform(rotation=unreal.Rotator(roll=r[0],pitch=r[1],yaw=r[2]))
     h.set_control_value(key(n,unreal.RigElementType.CONTROL),unreal.RigHierarchy.make_control_value_from_euler_transform(e))
    assert rig.execute('Forwards Solve')
   def direction(f):
    q=h.get_global_transform(key(f+'_03_'+side)).rotation;x,y,z,w=q.x,q.y,q.z,q.w;sign=1 if side=='l' else -1
    return [sign*v for v in (1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y))]
   def tip(f):
    p=vec(h.get_global_transform(key(f+'_03_'+side)).translation);size={'thumb':2.6,'index':2.2,'middle':2.5}[f]
    return [x+y*size for x,y in zip(p,direction(f))]
   evaluate();targets=[tip(f) for f in fingers];target=[sum(p[i] for p in targets)/len(targets) for i in range(3)]
   params=[('thumb_01_'+side+'_ctrl',0,-10,85),('thumb_01_'+side+'_ctrl',1,-40,65),('thumb_01_'+side+'_ctrl',2,-65,30),('thumb_02_'+side+'_ctrl',2,-55,20),('thumb_03_'+side+'_ctrl',2,-40,10)]
   def loss():return dot(sub(tip('thumb'),target),sub(tip('thumb'),target))+.015*(1+dot(direction('thumb'),direction(fingers[0])))**2
   best=loss()
   for step in (12,6,3,1,.5,.2,.05):
    for _ in range(25):
     changed=False
     for n,a,lo,hi in params:
      old=values[n][a];bestv=old
      for sign in (-1,1):
       v=old+sign*step
       if not lo<=v<=hi:continue
       values[n][a]=v;evaluate();score=loss()
       if score<best-1e-10:best=score;bestv=v;changed=True
      values[n][a]=bestv;evaluate()
     if not changed:break
   gap=length(sub(tip('thumb'),target))
   out[states]={'controls':{n:[round(v,4) for v in r] for n,r in values.items()},'contact_error_cm':gap,'finger_tip_separation_cm':length(sub(targets[0],targets[-1]))}
  report['sides'][side]=out
 report['passed']=True
except Exception:report['exception']=traceback.format_exc()
(ROOT/'reports/hand_contact_calibration.json').write_text(json.dumps(report,indent=2))
unreal.log('HAND_CONTACT_CALIBRATION '+str(report['passed']))
