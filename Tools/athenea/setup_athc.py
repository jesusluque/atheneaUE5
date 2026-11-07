# Copyright (c) 2026 jesus luque.
# Imports athenea's .athc of the pawn and builds /Game/TestAthc with it.
import unreal, os

path = os.path.join(os.path.expanduser("~"), "athenea-unreal", "pawn.athc")
task = unreal.AssetImportTask()
task.filename = path
task.destination_path = "/Game/Splats"
task.destination_name = "pawn_athc"
task.automated = True
task.replace_existing = True
task.save = True
task.factory = unreal.GSplatFactory()
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
unreal.log_warning("athenea: imported %s" % list(task.imported_object_paths))

asset = unreal.load_asset("/Game/Splats/pawn_athc")
splats = asset.get_editor_property("splats")
unreal.log_warning("athenea: %d splats" % splats.get_editor_property("splat_count"))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not unreal.EditorAssetLibrary.does_asset_exist("/Game/TestAthc"):
    les.new_level("/Game/TestAthc")
else:
    les.load_level("/Game/TestAthc")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.GaussianSplatActor):
        eas.destroy_actor(a)
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", asset)
les.save_current_level()
unreal.log_warning("athenea: TestAthc saved")
