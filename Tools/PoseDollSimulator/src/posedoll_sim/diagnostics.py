"""Conservative mechanical diagnostics, never a substitute for CAD shell clearance."""
import numpy as np


def segment_distance(a,b,c,d):
    u,v,w=b-a,d-c,a-c
    aa,bb,cc,dd,ee=np.dot(u,u),np.dot(u,v),np.dot(v,v),np.dot(u,w),np.dot(v,w)
    if aa<1e-16 or cc<1e-16:return float('inf')
    denominator=aa*cc-bb*bb
    s=np.clip((bb*ee-cc*dd)/denominator,0,1) if denominator>1e-16 else 0.
    t=np.clip((bb*s+ee)/cc,0,1)
    s=np.clip((bb*t-dd)/aa,0,1)
    return float(np.linalg.norm(w+s*u-t*v))


def diagnose(profile,q):
    pose=profile.fk(q)
    axes={}
    limits=[]
    for node in profile.nodes:
        if node['kind']!='revolute':continue
        aid=node['axis_id']
        before=(np.eye(4) if node['parent'] is None else pose[node['parent']])@profile.pre[node['id']]
        axes.setdefault(aid.split('.')[0],[]).append(before[:3,:3]@node['axis_local'])
        lo,hi=profile.axes[aid]['limits_rad']
        if q[aid]<lo-1e-8 or q[aid]>hi+1e-8:limits.append(aid)
    singular=[]
    conditions={}
    for group,vectors in axes.items():
        if len(vectors)!=3:continue
        values=np.linalg.svd(np.array(vectors).T,compute_uv=False)
        ratio=float(values[-1]/values[0])
        conditions[group]=ratio
        if ratio<.05:singular.append(group)
    # Only nonadjacent limb pairs are compared. Radii are diagnostic assumptions (5 mm).
    links=[]
    for side in 'lr':
        for a,b in [('upperarm','lowerarm'),('lowerarm','hand'),('thigh','calf'),('calf','foot')]:
            # Semantic anchors and their node names are not necessarily identical.
            segments=profile.data['anatomical_segments']
            def anchor(name):
                if isinstance(segments,dict):return segments[name] if isinstance(segments[name],str) else segments[name]['node_id']
                return next(s.get('node_id',s.get('node')) for s in segments if s.get('id',s.get('name'))==name)
            na,nb=anchor(a+'_'+side),anchor(b+'_'+side)
            links.append((a+'_'+side,na,nb,pose[na][:3,3],pose[nb][:3,3]))
    collisions=[]
    for i,x in enumerate(links):
        for y in links[i+1:]:
            if {x[1],x[2]} & {y[1],y[2]}:continue
            if segment_distance(x[3],x[4],y[3],y[4])<.01:collisions.append((x[0],y[0]))
    return {'limits':limits,'near_singular':singular,'jacobian_ratio':conditions,'rough_collisions':collisions}
