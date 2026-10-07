"""Calibrate straight phalanges against the evaluated Manny rig, inside Unreal."""
import json, math, traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
report={'passed':False,'sides':{}}
def key(n,t=unreal.RigElementType.BONE):return unreal.RigElementKey(name=n,type=t)
def vec(v):return [v.x,v.y,v.z]
def sub(a,b):return [x-y for x,y in zip(a,b)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def unit(a):
 l=math.sqrt(dot(a,a));return [x/l for x in a]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def angle(a,b):return math.degrees(math.acos(max(-1,min(1,dot(unit(a),unit(b))))))
try:
 asset=unreal.load_asset('/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body')
 for side in ('l','r'):
  rig=asset.create_control_rig();rig.request_construction();assert rig.execute('Construction');assert rig.execute('Forwards Solve');h=rig.get_hierarchy();values={}
  def pos(n):return vec(h.get_global_transform(key(n+'_'+side)).translation)
  def direction(f,j):
   if j<3:return unit(sub(pos(f+'_%02d'%(j+1)),pos(f+'_%02d'%j)))
   q=h.get_global_transform(key(f+'_03_'+side)).rotation
   x,y,z,w=q.x,q.y,q.z,q.w;sign=1 if side=='l' else -1
   return [sign*v for v in [1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)]]
  def setrot(f,j,r):
   n=f+'_%02d_'%j+side+'_ctrl';values[n]=r[:]
   h.reset_pose_to_initial(unreal.RigElementType.ALL)
   for cn,cr in values.items():
    e=unreal.EulerTransform(rotation=unreal.Rotator(roll=cr[0],pitch=cr[1],yaw=cr[2]))
    h.set_control_value(key(cn,unreal.RigElementType.CONTROL),unreal.RigHierarchy.make_control_value_from_euler_transform(e))
   assert rig.execute('Forwards Solve')
  across=unit(sub(pos('pinky_01'),pos('index_01')))
  up=unit(sub(pos('middle_01'),pos('hand')))
  normal=unit(cross(across,up))
  errors={};spreads={}
  def fit(f,j,target,start):
   r=start[:];setrot(f,j,r)
   def loss():return sum((x-y)**2 for x,y in zip(direction(f,j),unit(target)))
   best=loss()
   for step in (8,4,2,1,.5,.2,.05,.01,.002):
    for _ in range(15):
     changed=False
     for axis in (1,2):
      best_r=r[:]
      for sign in (-1,1):
       candidate=r[:];candidate[axis]+=step*sign
       if abs(candidate[axis])>60:continue
       setrot(f,j,candidate);value=loss()
       if value<best-1e-13:best=value;best_r=candidate;changed=True
      r=best_r;setrot(f,j,r)
     if not changed:break
   return [round(x,4) for x in r]
  for f in ('thumb','index','middle','ring','pinky'):
   if f=='thumb':setrot(f,1,[35,0,0])
   else:
    d=direction(f,1);target=sub(d,[x*dot(d,normal) for x in normal]);fit(f,1,target,[0,0,0])
   for j in (2,3):fit(f,j,direction(f,j-1),[0,0,0])
   errors[f]=[angle(direction(f,1),direction(f,2)),angle(direction(f,2),direction(f,3))]
   spreads[f]=direction(f,1)
  report['sides'][side]={'controls':values,'joint_errors_degrees':errors,'root_directions':spreads,'across':across,'up':up,'normal':normal,'bones':{str(k.name):{'p':vec(h.get_global_transform(k).translation),'q':vec(h.get_global_transform(k).rotation)+[h.get_global_transform(k).rotation.w]} for k in h.get_all_keys() if k.type==unreal.RigElementType.BONE and str(k.name).startswith(('hand_','thumb_','index_','middle_','ring_','pinky_'))}}
  assert max(a for row in errors.values() for a in row)<.05,errors
  straight={n:r[:] for n,r in values.items()}
  common=spreads['middle'];lateral=unit(sub(across,[x*dot(across,common) for x in common]));styles={}
  for style,angles in [('closed',[0,0,0,0]),('wide',[-18,-5,8,22])]:
   values={n:r[:] for n,r in straight.items()};roots={}
   for f,degrees in zip(('index','middle','ring','pinky'),angles):
    a=math.radians(degrees);target=[x*math.cos(a)+y*math.sin(a) for x,y in zip(common,lateral)]
    roots[f]=fit(f,1,target,straight[f+'_01_'+side+'_ctrl'])
   styles[style]=roots
  report['sides'][side]['root_styles']=styles
 report['passed']=True
except Exception:report['exception']=traceback.format_exc()
(ROOT/'reports/hand_straight_calibration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('HAND_STRAIGHT_CALIBRATION '+str(report['passed']))
