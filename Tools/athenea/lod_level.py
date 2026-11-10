# Copyright (c) 2026 jesus luque.
# A .athc of a scene beside the scene's own level, seen the same: the file
# imported, its actor turned so that it lies as the .ply/.splat importer lays
# the same file out (the .athc importer maps the file's axes to (x, z, y), the
# .ply one to (z, x, -y): half a turn about (1, 1, 0)), and the source level's
# BenchCamera copied. /Game/F_<NAME>_lod.
#   py lod_level.py SOURCE_LEVEL FILE NAME [--quit]   (FILE in ~/athenea-unreal)
import unreal, os, sys
a = [x for x in sys.argv[1:] if not x.startswith("--")]
source, f, name = a[0], a[1], a[2]
home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(source)
cam = [x for x in eas.get_all_level_actors() if isinstance(x, unreal.CameraActor) and x.get_actor_label().startswith("BenchCamera")][0]
eye, rot, fov = cam.get_actor_location(), cam.get_actor_rotation(), cam.camera_component.get_editor_property("field_of_view")
task = unreal.AssetImportTask()
task.filename = os.path.join(home, f)
task.destination_path = "/Game/Files"
task.destination_name = name
task.automated = True
task.replace_existing = True
task.save = True
task.factory = unreal.GSplatFactory()
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
level = "/Game/F_" + name
if unreal.EditorAssetLibrary.does_asset_exist(level):
    les.load_level(level)
    for x in eas.get_all_level_actors():
        eas.destroy_actor(x)
else:
    les.new_level(level)
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
actor.set_actor_rotation(unreal.Quat(0.70710678, 0.70710678, 0.0, 0.0).rotator(), False)
actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", unreal.load_asset("/Game/Files/" + name))
c = eas.spawn_actor_from_class(unreal.CameraActor, eye)
c.set_actor_label("BenchCamera")
c.set_actor_rotation(rot, False)
c.camera_component.set_editor_property("field_of_view", fov)
c.camera_component.set_editor_property("constrain_aspect_ratio", False)
c.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: %s made from %s, seen as %s" % (level, f, source))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
