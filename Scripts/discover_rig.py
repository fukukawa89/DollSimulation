"""Read-only UE editor discovery; run with -ExecutePythonScript (engine Python, stdlib only)."""
import json
from pathlib import Path
import unreal

OUTPUT = Path(unreal.Paths.project_dir()) / 'reports' / 'rig_inventory.json'
MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
RIG = '/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body'


def transform(t):
    return {'translation': [t.translation.x, t.translation.y, t.translation.z],
            'rotation_xyzw': [t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w],
            'scale': [t.scale3d.x, t.scale3d.y, t.scale3d.z]}


def run():
    rig = unreal.load_asset(RIG)
    hierarchy = rig.hierarchy
    items = []
    for key in hierarchy.get_all_keys():
        row = {'name': str(key.name), 'type': str(key.type),
               'parents': [str(p.name) for p in hierarchy.get_parents(key)],
               'initial_local': transform(hierarchy.get_local_transform(key, initial=True)),
               'initial_global': transform(hierarchy.get_global_transform(key, initial=True)),
               'current_local': transform(hierarchy.get_local_transform(key, initial=False))}
        if key.type == unreal.RigElementType.CONTROL:
            settings = hierarchy.get_control_settings(key)
            row['control_type'] = str(settings.control_type)
            row['primary_axis'] = str(settings.primary_axis)
            row['settings'] = str(settings)
            value = hierarchy.get_control_value(key, unreal.RigControlValueType.INITIAL)
            row['value'] = str(value)
        items.append(row)
    mesh = unreal.load_asset(MESH)
    reference = mesh.skeleton.get_reference_pose()
    report = {'engine': unreal.SystemLibrary.get_engine_version(), 'rig_asset': RIG,
              'mesh_asset': MESH, 'items': items, 'status': 'DISCOVERED_NOT_ADAPTED',
              'mesh_description': str(mesh), 'reference_pose_api': [name for name in dir(reference) if not name.startswith('_')],
              'rig_generated_class': rig.generated_class().get_path_name()}
    OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('POSEDOLL_DISCOVERY_SUCCESS ' + str(len(items)))


run()
