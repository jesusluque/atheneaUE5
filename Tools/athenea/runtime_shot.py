# Copyright (c) 2026 jesus luque.
# A level's splat actor given an asset loaded at runtime from FILE
# (GSplatRuntimeLibrary.LoadGaussianSplatFile, as a packaged game loads one),
# then shot from the level's BenchCamera (render_demo.py), unsaved.
#   py runtime_shot.py LEVEL FILE NAME
import unreal, os, sys, time
level, f, name = sys.argv[1], sys.argv[2], sys.argv[3]
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
t0 = time.time()
asset = unreal.GSplatRuntimeLibrary.load_gaussian_splat_file(os.path.join(os.path.expanduser("~"), "athenea-unreal", f))
unreal.log_warning("athenea: runtime load of %s: %s in %.1f s" % (f, asset, time.time() - t0))
actors = eas.get_all_level_actors()
splat = [a for a in actors if isinstance(a, unreal.GaussianSplatActor)][0]
splat.get_editor_property("gaussian_splat_component").set_gaussian_splat_asset(asset)
cam = [a for a in actors if isinstance(a, unreal.CameraActor) and a.get_actor_label().startswith("BenchCamera")][0]
rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, cam.get_actor_location(), cam.get_actor_rotation())
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", cam.camera_component.get_editor_property("field_of_view"))
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
start = time.time()
state = {"done": 0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    if state["done"] == 0 and e >= 42:
        unreal.RenderingLibrary.export_render_target(world, rt, out, name)
        unreal.log_warning("athenea: exported %s" % name)
        state["done"] = 1
    elif state["done"] == 1 and e >= 46:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
