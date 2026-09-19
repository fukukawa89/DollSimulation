"""Fresh process verification of saved contact-derived FK poses without live input."""
import json,sys,math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());sys.path.insert(0,str(ROOT/'Scripts'))
from test_capture import snapshot
report={'passed':False,'cases':[]};out=ROOT/'reports/contacts_reopen.json'
try:
    expected=json.loads((ROOT/'reports/contacts_extended.json').read_text(encoding='utf-8'));assert expected['passed']
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/PoseDollLab/PoseDoll_Test')
    seq=unreal.load_asset(expected['sequence']);assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    proxy=next(p for p in unreal.ControlRigSequencerLibrary.get_control_rigs(seq) if str(p.track.get_display_name())=='PoseDoll / Manny')
    for frame,bones in expected['poses'].items():
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(int(frame));actual=snapshot(proxy.control_rig)
        position=max(math.dist(v['p'],actual[n]['p']) for n,v in bones.items())
        rotation=max(math.degrees(2*math.acos(min(1,abs(sum(a*b for a,b in zip(v['q'],actual[n]['q'])))))) for n,v in bones.items())
        report['cases'].append({'frame':int(frame),'position_cm':position,'rotation_deg':rotation})
        assert position<=.5 and rotation<=1
    report.update(passed=True,live_input_used=False,cmdline=unreal.SystemLibrary.get_command_line())
    unreal.log('POSEDOLL_CONTACTS_REOPEN_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc);raise
finally:
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
