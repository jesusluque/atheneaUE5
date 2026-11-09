# Copyright (c) 2026 jesus luque.
# An HDRI as a level's backdrop and its sky light: the .hdr imported as a cube,
# an unlit two-sided material that shows it along the view, on a sphere 250 m
# across round the origin, and the sky light lit by the same cube.
#   py setup_backdrop.py LEVEL HDR [YAW] [--quit]   (HDR in ~/athenea-unreal)
import unreal, os, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
level, hdr = args[0], args[1]
yaw = float(args[2]) if len(args) > 2 else 0.0
home = os.path.join(os.path.expanduser("~"), "athenea-unreal")
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mel = unreal.MaterialEditingLibrary

name = os.path.splitext(os.path.basename(hdr))[0].replace("-", "_")
if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Env/" + name):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(home, hdr)
    task.destination_path = "/Game/Env"
    task.destination_name = name
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
cube = unreal.load_asset("/Game/Env/" + name)

path = "/Game/Env/M_Backdrop_" + name
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
mat = tools.create_asset("M_Backdrop_" + name, "/Game/Env", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
view = mel.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -600, 0)
neg = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 0)
neg.set_editor_property("const_b", -1.0)
mel.connect_material_expressions(view, "", neg, "A")
sample = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -200, 0)
sample.set_editor_property("texture", cube)
sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
mel.connect_material_expressions(neg, "", sample, "UVs")
mel.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)

les.load_level(level)
for a in eas.get_all_level_actors():
    if a.get_actor_label().startswith("Backdrop"):
        eas.destroy_actor(a)
sphere = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Sphere"), unreal.Vector(0, 0, 0))
sphere.set_actor_label("Backdrop")
sphere.set_actor_scale3d(unreal.Vector(5000, 5000, 5000))
sphere.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
smc = sphere.get_editor_property("static_mesh_component")
smc.set_material(0, mat)
smc.set_editor_property("cast_shadow", False)
for sky in [a for a in eas.get_all_level_actors() if isinstance(a, unreal.SkyLight)]:
    lc = sky.get_editor_property("light_component")
    lc.set_editor_property("cubemap", cube)
    lc.set_editor_property("source_cubemap_angle", yaw)
    lc.recapture_sky()
les.save_current_level()
unreal.log_warning("athenea: %s over %s" % (level, name))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
