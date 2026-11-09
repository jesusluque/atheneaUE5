# Copyright (c) 2026 jesus luque.
# A sequence of clouds (a medium's frames from athenea vdb2splat) imported from
# a folder and played by one actor in /Game/S_<name>, under the studio sky:
#   py setup_sequence.py FOLDER NAME [FPS] [--quit]   (FOLDER in ~/athenea-unreal;
# its *.usda, else *.usdc, else *.ply, in name order)
import unreal, os, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
folder, name = args[0], args[1]
fps = float(args[2]) if len(args) > 2 else 24.0
home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
src = os.path.join(home, folder)
files = sorted(f for f in os.listdir(src) if f.endswith(".usda"))
if not files:
    files = sorted(f for f in os.listdir(src) if f.endswith(".usdc") or f.endswith(".ply"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

tasks = []
for f in files:
    task = unreal.AssetImportTask()
    task.filename = os.path.join(src, f)
    task.destination_path = "/Game/Seq/" + name
    task.destination_name = os.path.splitext(f)[0]
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = unreal.GSplatFactory()
    tasks.append(task)
tools.import_asset_tasks(tasks)
frames = [unreal.load_asset("/Game/Seq/%s/%s" % (name, os.path.splitext(f)[0])) for f in files]
frames = [f for f in frames if f]
unreal.log_warning("athenea: %d frames of %s imported" % (len(frames), name))

if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Tx/studio"):
    sky_task = unreal.AssetImportTask()
    sky_task.filename = os.path.join(home, "studio.hdr")
    sky_task.destination_path = "/Game/Tx"
    sky_task.destination_name = "studio"
    sky_task.automated = True
    sky_task.save = True
    tools.import_asset_tasks([sky_task])
sky = unreal.load_asset("/Game/Tx/studio")

level = "/Game/S_" + name
if unreal.EditorAssetLibrary.does_asset_exist(level):
    les.load_level(level)
    for a in eas.get_all_level_actors():
        eas.destroy_actor(a)
else:
    les.new_level(level)
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
comp = actor.get_editor_property("gaussian_splat_component")
comp.set_editor_property("gaussian_splat_asset", frames[-1])
comp.set_editor_property("sequence_frames", frames)
comp.set_editor_property("sequence_frames_per_second", fps)
light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
lc = light.get_editor_property("light_component")
lc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
lc.set_editor_property("cubemap", sky)
lc.set_editor_property("lower_hemisphere_is_black", False)
lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
# A key light as athenea's look stages have one: 35 degrees up, from the side.
key = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 0))
key.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-35.0, yaw=-115.0), False)
kc = key.get_editor_property("light_component")
kc.set_editor_property("intensity", float(os.environ.get("ATHENEA_KEY_LUX", "3")))
kc.set_editor_property("light_color", unreal.Color(r=255, g=242, b=224, a=255))
kc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
# The player's view in -game (ue_time.sh): framed on the last frame's bounds
# from -x and a little above, as capture_sequence.py frames it.
origin, extent, _ = unreal.SystemLibrary.get_component_bounds(comp)
r = max(extent.x, extent.y, extent.z)
eye = origin + unreal.Vector(-2.6 * r, 0.0, 0.5 * r)
cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, origin), False)
cam.camera_component.set_editor_property("field_of_view", 45.0)
cam.camera_component.set_editor_property("aspect_ratio", 1.0)
cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: level %s saved, %d frames at %g fps" % (level, len(frames), fps))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
