"""Create a small lit stage only in the owned PoseDoll demonstration map."""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
existing={a.get_actor_label():a for a in actors.get_all_level_actors()}
def ensure(label,cls,position,rotation=unreal.Rotator()):
    if label in existing:return existing[label]
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*position),rotation);a.set_actor_label(label);return a
floor=ensure('PoseDoll_StageFloor',unreal.StaticMeshActor,(120,35,-7))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(8,8,.1))
key=ensure('PoseDoll_KeyLight',unreal.DirectionalLight,(0,0,300),unreal.Rotator(-45,-35,0))
key.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4.)
fill=ensure('PoseDoll_FillLight',unreal.PointLight,(120,-180,260))
c=fill.get_component_by_class(unreal.PointLightComponent);c.set_intensity(3000.);c.set_attenuation_radius(1200.)
assert level.save_current_level()
unreal.log('POSEDOLL_DEMO_STAGE_SUCCESS')
