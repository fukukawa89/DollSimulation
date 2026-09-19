"""Read-only compact status for the long-running GUI acceptance."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
r=json.loads((root/'reports/soak_gui.json').read_text(encoding='utf-8'))
s=r.get('final_status',r.get('latest_status',{}))
last=r.get('history',[])[-1] if r.get('history') else {}
print(json.dumps({'phase':r.get('phase'),'steady_seconds':round(r.get('steady_seconds',0),1),
    'panel_cycles':r.get('panel_cycles'),'sessions':len(r.get('unique_sessions',[])),
    'state':s.get('state'),'processing_p95_ms':s.get('processing_p95_ms'),
    'processing_preview_p95_ms':s.get('main_thread_with_preview_p95_ms'),
    'receive_preview_p95_ms':s.get('receive_to_preview_p95_ms'),
    'recent_fps':last.get('fps'),'memory_mb':last.get('memory_mb'),
    'applied':s.get('applied'),'invalid':s.get('invalid'),'error':r.get('error','')},ensure_ascii=False,indent=2))
