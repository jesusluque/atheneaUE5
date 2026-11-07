# Copyright (c) 2026 jesus luque.
# Linear colour of /Game/TestTx from its camera, post-processed but not tone mapped,
# through a SceneCapture2D into a float render target exported as EXR; and the
# TX cloud's projected sky logged.   py capture_tx.py ANGLE NAME [LEVEL] [CVAR=VALUE;...]
import unreal, os, sys, time

angle = float(sys.argv[1]) if len(sys.argv) > 1 else 150.0
name = sys.argv[2] if len(sys.argv) > 2 else "tx_linear"
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(sys.argv[3] if len(sys.argv) > 3 else "/Game/TestTx")
# Console commands to run first, ';' between them (a CVar for an A/B capture).
for command in (sys.argv[4] if len(sys.argv) > 4 else "").split(";"):
    if command.strip():
        unreal.SystemLibrary.execute_console_command(None, command.strip().replace("=", " "))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
cam = [a for a in actors if isinstance(a, unreal.CameraActor)][0]
light = [a for a in actors if isinstance(a, unreal.SkyLight)][0].get_editor_property("light_component")
light.set_editor_property("source_cubemap_angle", angle)
light.recapture_sky()

rt = unreal.RenderingLibrary.create_render_target2d(world, 512, 512, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, cam.get_actor_location(), cam.get_actor_rotation())
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", cam.camera_component.get_editor_property("field_of_view"))   # the level camera's
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
# UE's post-processing darkens the frame's edges (VignetteIntensity 0.4 by
# default): off, so the capture is the same light everywhere in it.
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
    if state["done"] == 0 and e >= 50:
        unreal.SystemLibrary.execute_console_command(None, "r.Gaussians.Athenea.LogSky 1")
        state["done"] = 1
    elif state["done"] == 1 and e >= 60:
        # A few renders, so the sky's readback has frames to arrive in.
        cc.capture_scene()
        if e >= 62:
            state["done"] = 2
    elif state["done"] == 2 and e >= 63:
        unreal.RenderingLibrary.export_render_target(world, rt, out, name)
        unreal.log_warning("athenea: exported %s" % name)
        state["done"] = 3
    elif state["done"] == 3 and e >= 68:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
