# Copyright (c) 2026 jesus luque.
# A demo level's hero shot: a SceneCapture2D where its BenchCamera stands, at
# its field of view, 1600 x 900, a float EXR; sequences held at T seconds.
#   py render_demo.py LEVEL NAME [T]
import unreal, os, sys, time

level, name = sys.argv[1], sys.argv[2]
at = float(sys.argv[3]) if len(sys.argv) > 3 else 1.6
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
for s in [a for a in actors if isinstance(a, unreal.GaussianSplatActor)]:
    c = s.get_editor_property("gaussian_splat_component")
    if len(c.get_editor_property("sequence_frames")) > 0:
        c.set_editor_property("sequence_playing", False)
        c.set_sequence_time(at)
cam = [a for a in actors if isinstance(a, unreal.CameraActor) and a.get_actor_label().startswith("BenchCamera")][0]
rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, cam.get_actor_location(), cam.get_actor_rotation())
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", cam.camera_component.get_editor_property("field_of_view"))
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
pp = cc.get_editor_property("post_process_settings")
pp.set_editor_property("override_vignette_intensity", True)
pp.set_editor_property("vignette_intensity", 0.0)
cc.set_editor_property("post_process_settings", pp)
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
