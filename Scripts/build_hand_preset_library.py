"""Author 125 absolute Manny hand shapes; render matching thumbnails in Unreal.

IDs 001-061 retain their original data. Variants share E/B/C classifications;
internal authoring metadata is for QA, never a user-facing pose name.
"""
import itertools,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FINGERS=('thumb','index','middle','ring','pinky')
REFERENCE='EEBBB ECCCE CCCCC CEECC EEEEE CEEEE CECCC CEEEC BBCCC EEECC ECCCC BCCCC BBBBB EECCC EBCCC CBBBB CCECC BBEEE BEEEE CBBCC BEBBB EEEBB EEEEB EECCE EBBEE BEECC BBEBB'.split()
STATES=list(dict.fromkeys(['EEEEE','CCCCC','BBBBB']+REFERENCE+[''.join(p) for p in itertools.product('EC',repeat=5)]))
for p in itertools.product('EBC',repeat=5):
    code=''.join(p)
    if len(STATES)>=61:break
    if code not in STATES and code.count('B') in (1,2):STATES.append(code)
CURL={'E':(0,0,0),'B':(-25,-30,-15),'C':(-50,-65,-35)}
THUMB={'E':((35,0,0),(0,0,0),(0,0,0)), 'B':((32,10,-10),(0,0,-12),(0,0,-10)), 'C':((30,20,-20),(0,0,-25),(0,0,-20))}
CALIBRATION=json.loads((ROOT/'Scripts/hand_preset_calibration.json').read_text(encoding='utf-8-sig'))

def controls(states,side,straight=(),spacing=None,bend=None,thumb=None):
    out={};cal=CALIBRATION['sides'][side]
    for f,state in zip(FINGERS,states):
        if f!='thumb':out[f'{f}_metacarpal_{side}_ctrl']=[0,0,0,0,0,0,1,1,1]
        for j in range(3):
            name=f'{f}_{j+1:02}_{side}_ctrl'
            rotation=list(THUMB[state][j] if f=='thumb' else (0,0,CURL[state][j]))
            if f in straight:rotation=cal['controls'][name][:]
            if spacing and f!='thumb' and state=='E' and j==0:
                rotation=cal['root_styles'][spacing][f][:]
            if bend and f!='thumb' and state=='B':
                rotation=cal['controls'][name][:]
                rotation[2]-={'base':(60,0,0),'hook':(5,75,45)}[bend][j]
            if thumb and f=='thumb':rotation=list(thumb[j])
            out[name]=[0,0,0,*[round(v,4) for v in rotation],1,1,1]
    return out

rows=[]
def add(states,family='natural',straight=(),spacing=None,bend=None,thumb=None):
    row={'id':f'hand-{len(rows)+1:03}','states':states,'authoring':{'family':family,'straight_fingers':list(straight)}}
    if spacing:row['authoring']['spacing']=spacing
    if bend:row['authoring']['bend']=bend
    for side,label in [('l','left'),('r','right')]:row[label]=controls(states,side,straight,spacing,bend,thumb)
    rows.append(row)
for states in STATES:add(states)
# All binary extended/curled combinations receive an anatomically straight
# counterpart, plus the common extended-thumb/index with three bent fingers.
for states in [s for s in STATES if 'B' not in s and 'E' in s]+['EEBBB']:
    add(states,'straight',[f for f,s in zip(FINGERS,states) if s=='E'])
for states in ('EEEEE','CEEEE','CEECC','EECCC'):
    for spacing in ('closed','wide'):
        add(states,'spacing',[f for f,s in zip(FINGERS,states) if s=='E'],spacing=spacing)
for states in ('BBBBB','CBBBB','EBBBB','CBCCC'):
    for bend in ('base','hook'):
        add(states,'bend_distribution',[f for f,s in zip(FINGERS,states) if s=='E'],bend=bend)
for states,thumb in [
    ('EEEEE',((35,-18,12),(0,-.38,23.318),(0,0,10))),
    ('EEEEE',((10,-25,5),(0,-.38,23.318),(0,0,10))),
    ('BEEEE',((35,0,-12),(0,0,-12),(0,0,-5))),
    ('CEEEE',((30,30,-30),(0,0,-25),(0,0,-20))),
    ('CCCCC',((45,0,-35),(0,0,-40),(0,0,-30))),
    ('CCCCC',((10,25,-25),(0,0,-20),(0,0,-15))),
]:add(states,'thumb_position',[f for f,s in zip(FINGERS,states) if s=='E' and f!='thumb'],thumb=thumb)
for states,straight in [
    ('EEEEE',('index','middle')),
    ('CEECC',('index',)),
    ('CEEEE',('index','middle')),
    ('EEECC',('index','middle')),
    ('EEEEE',('index','middle','ring','pinky')),
]:add(states,'mixed_extension',straight)
for style,flexion in [('loose',(55,65,35)),('tight',(75,90,45))]:
    add('CCCCC','curl_depth');row=rows[-1];row['authoring']['curl']=style
    for side,label in [('l','left'),('r','right')]:
        for f in FINGERS[1:]:
            for j,angle in enumerate(flexion,1):
                n=f'{f}_{j:02}_{side}_ctrl';rotation=CALIBRATION['sides'][side]['controls'][n][:];rotation[2]-=angle
                row[label][n][3:6]=[round(v,4) for v in rotation]
contacts=json.loads((ROOT/'Scripts/hand_contact_calibration.json').read_text())
for states in ('BBEEE','BEBEE','BBBEE'):
    add(states,'tip_contact',[f for f,s in zip(FINGERS,states) if s=='E']);row=rows[-1]
    for side,label in [('l','left'),('r','right')]:
        for n,r in contacts['sides'][side][states]['controls'].items():row[label][n][3:6]=r
assert len(rows)==125,len(rows)
assert len({json.dumps([r['left'],r['right']],sort_keys=True) for r in rows})==len(rows),'duplicate shape'
# Keep each original family together so visually comparable variants are adjacent.
order={s:i for i,s in enumerate(dict.fromkeys(STATES+[r['states'] for r in rows]))}
rows.sort(key=lambda r:order[r['states']])
out=ROOT/'Plugins/PoseDoll/Resources/HandPresets';out.mkdir(parents=True,exist_ok=True)
(out/'presets.json').write_text(json.dumps({'version':1,'authored_for':'SKM_Manny_Simple / CR_Mannequin_Body','presets':rows},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'Authored {len(rows)} shapes in {len(set(r["states"] for r in rows))} state combinations. Regenerate Unreal PNGs before shipping.')
