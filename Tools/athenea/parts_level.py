# Copyright (c) 2026 jesus luque.
# A scene cut in several .athc files (each under athenea's 4 GiB of records),
# imported and placed together in one level, /Game/F_<NAME>, under the studio
# sky; every asset saved.
#   py parts_level.py NAME FILE [FILE ...] [--quit]   (files in ~/athenea-unreal)
import unreal, os, sys, time
a = [x for x in sys.argv[1:] if not x.startswith("--")]
name, files = a[0], a[1:]
home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = []
for f in files:
    t0 = time.time()
    task = unreal.AssetImportTask()
    task.filename = os.path.join(home, f)
    task.destination_path = "/Game/Files"
    task.destination_name = os.path.splitext(f)[0]
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = unreal.GSplatFactory()
    tools.import_asset_tasks([task])
    assets.append(unreal.load_asset("/Game/Files/" + os.path.splitext(f)[0]))
    unreal.log_warning("athenea: %s imported in %.1f s" % (f, time.time() - t0))
level = "/Game/F_" + name
if unreal.EditorAssetLibrary.does_asset_exist(level):
    les.load_level(level)
    for x in eas.get_all_level_actors():
        eas.destroy_actor(x)
else:
    les.new_level(level)
for asset in assets:
    actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
    actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", asset)
light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
lc = light.get_editor_property("light_component")
lc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
lc.set_editor_property("cubemap", unreal.load_asset("/Game/Tx/studio"))
lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
les.save_current_level()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
unreal.log_warning("athenea: level %s with %d parts saved" % (level, len(assets)))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
