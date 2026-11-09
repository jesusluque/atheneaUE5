# Copyright (c) 2026 jesus luque.
# A new level with one splat actor of an existing asset, under the studio sky:
#   py level_only.py ASSET NEWLEVEL [--quit]
import unreal, sys
args = [a for a in sys.argv[1:] if not a.startswith("--")]
asset_path, new = args[0], args[1]
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(new):
    les.load_level(new)
    for a in eas.get_all_level_actors():
        eas.destroy_actor(a)
else:
    les.new_level(new)
actor = eas.spawn_actor_from_class(unreal.GaussianSplatActor, unreal.Vector(0, 0, 0))
actor.get_editor_property("gaussian_splat_component").set_editor_property("gaussian_splat_asset", unreal.load_asset(asset_path))
light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
lc = light.get_editor_property("light_component")
lc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
lc.set_editor_property("cubemap", unreal.load_asset("/Game/Tx/studio"))
lc.set_editor_property("lower_hemisphere_is_black", False)
lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
les.save_current_level()
unreal.log_warning("athenea: %s alone in %s" % (asset_path, new))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
