# Copyright (c) 2026 jesus luque.
# Imports the pawn PLY and builds /Game/Test with the splat actor in it.
import unreal, os

ply = os.path.join(os.path.expanduser("~"), "athenea-unreal", "pawn_3dgs.ply")
task = unreal.AssetImportTask()
task.filename = ply
task.destination_path = "/Game/Splats"
task.destination_name = "pawn"
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
unreal.log("athenea: imported %s" % list(task.imported_object_paths))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.new_level("/Game/Test")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset = unreal.load_asset("/Game/Splats/pawn")
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
comp = actor.get_editor_property("gaussian_splat_component")
comp.set_editor_property("gaussian_splat_asset", asset)
# The importer reads the file's own axes from its comment.
les.save_current_level()
unreal.log("athenea: level saved")
