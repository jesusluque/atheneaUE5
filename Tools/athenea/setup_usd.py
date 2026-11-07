# Copyright (c) 2026 jesus luque.
# Imports athenea's USD ParticleField of the pawn and builds /Game/TestUsd with it.
import unreal, os

path = os.path.join(os.path.expanduser("~"), "athenea-unreal", "pawn.usdc")
task = unreal.AssetImportTask()
task.filename = path
task.destination_path = "/Game/Splats"
task.destination_name = "pawn_usd"
task.automated = True
task.replace_existing = True
task.save = True
task.factory = unreal.GSplatFactory()
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
unreal.log_warning("athenea: imported %s" % list(task.imported_object_paths))

asset = unreal.load_asset("/Game/Splats/pawn_usd")
splats = asset.get_editor_property("splats")
unreal.log_warning("athenea: %d splats" % splats.get_editor_property("splat_count"))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not unreal.EditorAssetLibrary.does_asset_exist("/Game/TestUsd"):
    les.new_level("/Game/TestUsd")
else:
    les.load_level("/Game/TestUsd")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.GaussianSplatActor):
        eas.destroy_actor(a)
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", asset)
les.save_current_level()
unreal.log_warning("athenea: TestUsd saved")
