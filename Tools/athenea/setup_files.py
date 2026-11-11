# Copyright (c) 2026 jesus luque.
# Imports splat files and builds a level for each, /Game/F_<name>, with an
# actor of it:   py setup_files.py file1 file2[@scale][!relit] ... [--quit]  (files in ~/athenea-unreal;
# @scale scales the actor, for a file in units other than metres; !relit
# imports a PLY with its normals and metallic/roughness, read or filled with
# 0 and 1, for the relit route under UE's lights)
import unreal, os, sys

home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# The studio HDRI, the sky light every level gets: a TX cloud is lit by it (and
# is black without one); a baked cloud ignores it.
sky_task = unreal.AssetImportTask()
sky_task.filename = os.path.join(home, "studio.hdr")
sky_task.destination_path = "/Game/Tx"
sky_task.destination_name = "studio"
sky_task.automated = True
sky_task.replace_existing = False
sky_task.save = True
if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Tx/studio"):
    tools.import_asset_tasks([sky_task])
sky = unreal.load_asset("/Game/Tx/studio")
for arg in [a for a in sys.argv[1:] if not a.startswith("--")]:
    arg, relit = (arg[:-6], True) if arg.endswith("!relit") else (arg, False)
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
    if relit:
        options = unreal.GSplatImportData()
        options.set_editor_property("import_normals", True)
        options.set_editor_property("import_metallic_roughness", True)
        options.set_editor_property("populate_mr_with_default", True)
        task.options = options
    tools.import_asset_tasks([task])
    # A scene's other clouds are assets the import made beside the one it
    # returns, and only that one is saved with it: the rest are saved here.
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
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
    # A scene's other clouds (an .athz web scene: its objects, a body split in
    # several .athc files), each its own asset named after the first: placed
    # beside it, as the scene places them.
    for other in sorted(unreal.EditorAssetLibrary.list_assets("/Game/Files", recursive=False)):
        other_name = other.split("/")[-1].split(".")[0]
        if other_name.startswith(name + "_") and f.endswith(".athz"):
            extra = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
            if scale:
                extra.set_actor_scale3d(unreal.Vector(float(scale), float(scale), float(scale)))
            extra.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", unreal.load_asset(other))
            unreal.log_warning("athenea: %s placed beside it" % other_name)
    light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
    comp = light.get_editor_property("light_component")
    comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    comp.set_editor_property("cubemap", sky)
    comp.set_editor_property("lower_hemisphere_is_black", False)
    comp.set_editor_property("cubemap_resolution", 1024)
    comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    les.save_current_level()
    unreal.log_warning("athenea: level %s saved" % level)

if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
