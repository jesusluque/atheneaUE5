# Copyright (c) 2026 jesus luque.
# A level of several clouds under one HDRI, seen from a camera given in the
# files' own frame (athenea's: y up, metres):
#   py setup_scene.py LEVEL SKY.hdr SCALE EYE TARGET FOV file1 file2 ... [--quit]
# EYE and TARGET as x:y:z (-ExecCmds splits on commas); SCALE the actors' (and the camera's) scale, for a
# scene small against UE's 10 cm near plane; the sky light as setup_tx.py sets
# it. Files in the home folder's athenea-unreal; the camera is named TxCamera,
# so capture_tx.py captures from it.
import unreal, os, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
level, sky_file, scale = args[0], args[1], float(args[2])
eye_f = [float(v) for v in args[3].split(":")]
target_f = [float(v) for v in args[4].split(":")]
fov = float(args[5])
files = args[6:]
home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
tools = unreal.AssetToolsHelpers.get_asset_tools()

def imported(path, name, factory=None):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(home, path)
    task.destination_path = "/Game/Scene"
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    if factory:
        task.factory = factory
    tools.import_asset_tasks([task])
    return unreal.load_asset("/Game/Scene/" + name)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
clouds = [imported(f, os.path.splitext(f)[0].replace("-", "_").replace(".", "_"), unreal.GSplatFactory()) for f in files]
sky = imported(sky_file, os.path.splitext(sky_file)[0].replace("-", "_").replace(".", "_"))
if unreal.EditorAssetLibrary.does_asset_exist(level):
    les.load_level(level)
else:
    les.new_level(level)
for a in eas.get_all_level_actors():
    eas.destroy_actor(a)
for c in clouds:
    actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
    actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", c)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))

light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
comp = light.get_editor_property("light_component")
comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
comp.set_editor_property("cubemap", sky)
comp.set_editor_property("intensity", 1.0)
comp.set_editor_property("real_time_capture", False)
comp.set_editor_property("lower_hemisphere_is_black", False)
comp.set_editor_property("cubemap_resolution", 1024)
comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

# athenea's y-up metres into UE: (x, z, y), times 100 for centimetres and the scale.
to_ue = lambda p: unreal.Vector(p[0], p[2], p[1]) * (100.0 * scale)
eye, target = to_ue(eye_f), to_ue(target_f)
cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
cam.set_actor_label("TxCamera")
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target), False)
cam.camera_component.set_editor_property("field_of_view", fov)
cam.camera_component.set_editor_property("aspect_ratio", 1.0)
cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: %s saved with %d clouds" % (level, len(clouds)))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
