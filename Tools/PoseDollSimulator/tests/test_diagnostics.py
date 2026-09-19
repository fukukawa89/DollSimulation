import math
import numpy as np
from posedoll_sim.core import DeviceProfile,default_shared
from posedoll_sim.diagnostics import diagnose,segment_distance

def test_segment_crossing_and_parallel():
    a=np.array([0.,0,0]);b=np.array([1.,0,0])
    assert segment_distance(a,b,np.array([.5,-1.,0]),np.array([.5,1.,0]))<1e-12
    assert abs(segment_distance(a,b,a+[0,2,0],b+[0,2,0])-2)<1e-12

def test_angular_jacobian_detects_gimbal_alignment():
    p=DeviceProfile(default_shared());q=dict.fromkeys(p.order,0.)
    normal=diagnose(p,q)
    assert normal['limits']==[] and normal['near_singular']==[]
    q['upperarm_l.abduct']=math.pi/2
    singular=diagnose(p,q)
    assert 'upperarm_l' in singular['near_singular']
