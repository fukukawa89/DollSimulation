"""Legal shoulder poses on opposite sides of the native control Euler branch."""
import json, math
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'Shared/Fixtures'
for label,flex in (('a',4),('b',6)):
    sample=json.loads((root/'neutral.sample.json').read_text(encoding='utf-8'))
    for index,degrees in ((14,flex),(15,140)):
        sample['raw_angles_rad'][index]=math.pi+math.radians(degrees)
    (root/f'euler_boundary_{label}.sample.json').write_text(json.dumps(sample,indent=2),encoding='utf-8')
