"""Check shipped Unreal thumbnail pairs and provenance with standard Python."""
from pathlib import Path
import json,hashlib,struct
ROOT=Path(__file__).resolve().parents[1]
root=ROOT/'Plugins/PoseDoll/Resources/HandPresets'
data=json.loads((root/'presets.json').read_text(encoding='utf-8-sig'))['presets']
hashes={'l':set(),'r':set()}
for p in data:
    assert len(p['states'])==5 and set(p['states'])<=set('EBC')
    for side in ('l','r'):
        path=root/'Thumbnails'/f'{p["id"]}_{side}.png'
        pixels=path.read_bytes()
        assert pixels[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',pixels[16:24])==(384,384),path
        m=json.loads(path.with_suffix('.json').read_text(encoding='utf-8-sig'))
        assert m['states']==p['states'] and m['preset']==p['id'] and m['side']==side,path
        assert m['renderer']=='Unreal Engine SceneCapture2D' and m['mesh'].endswith('SKM_Manny_Simple.SKM_Manny_Simple'),path
        assert len(m['controls_sha256'])==64,path
        hashes[side].add(hashlib.sha256(pixels).hexdigest())
assert len(hashes['l'])==len(data)==len(hashes['r'])
report={'passed':True,'count':len(data),'unique_left_images':len(hashes['l']),'unique_right_images':len(hashes['r'])}
(ROOT/'reports').mkdir(exist_ok=True)
(ROOT/'reports/hand_thumbnail_validation.json').write_text(json.dumps(report,indent=2))
print(report)
