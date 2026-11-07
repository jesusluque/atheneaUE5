# Copyright (c) 2026 jesus luque.
# Imports a TX cloud and an HDRI, and builds /Game/TestTx: the cloud, a sky
# light with the HDRI as its cubemap, and the test scene's camera.
import unreal, os, sys

# py setup_tx.py [cloud file] [level] [asset name] [actor scale]: another cloud
# into another level, under the same sky and camera (default paint_tx.usdc,
# /Game/TestTx, scale 100 -- 1 for a file in metres, such as a .athc).
args = [a for a in sys.argv[1:] if not a.startswith("--")]
cloud_file = args[0] if len(args) > 0 else "paint_tx.usdc"
level = args[1] if len(args) > 1 else "/Game/TestTx"
asset = args[2] if len(args) > 2 else "paint_tx"
scale = float(args[3]) if len(args) > 3 else 100.0

home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
tools = unreal.AssetToolsHelpers.get_asset_tools()

def imported(path, name, factory=None):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(home, path)
    task.destination_path = "/Game/Tx"
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    if factory:
        task.factory = factory
    tools.import_asset_tasks([task])
    unreal.log_warning("athenea: imported %s" % list(task.imported_object_paths))
    return unreal.load_asset("/Game/Tx/" + name)

cloud = imported(cloud_file, asset, unreal.GSplatFactory())
sky = imported("studio.hdr", "studio")
unreal.log_warning("athenea: sky is %s" % type(sky).__name__)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(level):
    les.load_level(level)
else:
    les.new_level(level)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    eas.destroy_actor(a)
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", cloud)
# The test scene's stage says centimetres (its root layer sets no units) while
# its numbers are metres, as athenea reads them: the actor takes the factor.
actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))

light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
comp = light.get_editor_property("light_component")
comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
comp.set_editor_property("cubemap", sky)
comp.set_editor_property("intensity", 1.0)
comp.set_editor_property("real_time_capture", False)
# athenea's dome is the whole sphere: the lower half is the image's, not black.
comp.set_editor_property("lower_hemisphere_is_black", False)
# A polished coat reflects the sky sharp: 1024 texels a face, not 128.
comp.set_editor_property("cubemap_resolution", 1024)
comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

# The scene's camera: (0, 0.4, 4.2) metres, 8 degrees down, 35 mm on a square
# 24.576 mm aperture; in UE's axes (x, z, y) times 100.
eye = unreal.Vector(0, 420, 40)
cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
cam.set_actor_label("TxCamera")
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(0, 4.07, -18.45)), False)
cam.camera_component.set_editor_property("field_of_view", 38.66)
cam.camera_component.set_editor_property("aspect_ratio", 1.0)
# The player's view in -game (ue_time.sh), so the timed frame is this one.
cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: %s saved" % level)
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
