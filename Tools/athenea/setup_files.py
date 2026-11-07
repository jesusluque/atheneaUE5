# Copyright (c) 2026 jesus luque.
# Imports splat files and builds a level for each, /Game/F_<name>, with an
# actor of it:   py setup_files.py file1 file2[@scale] ... [--quit]  (files in ~/athenea-unreal;
# @scale scales the actor, for a file in units other than metres)
import unreal, os, sys

home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for arg in [a for a in sys.argv[1:] if not a.startswith("--")]:
    f, _, scale = arg.partition("@")
    name = os.path.splitext(f)[0].replace("-", "_") + "_" + os.path.splitext(f)[1][1:]
    task = unreal.AssetImportTask()
    task.filename = os.path.join(home, f)
    task.destination_path = "/Game/Files"
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = unreal.GSplatFactory()
    tools.import_asset_tasks([task])
    asset = unreal.load_asset("/Game/Files/" + name)
    unreal.log_warning("athenea: imported %s -> %s" % (f, asset))
    level = "/Game/F_" + name
    if unreal.EditorAssetLibrary.does_asset_exist(level):
        les.load_level(level)
        for a in eas.get_all_level_actors():
            eas.destroy_actor(a)
    else:
        les.new_level(level)
    actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
    if scale:
        actor.set_actor_scale3d(unreal.Vector(float(scale), float(scale), float(scale)))
    actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", asset)
    les.save_current_level()
    unreal.log_warning("athenea: level %s saved" % level)

if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
