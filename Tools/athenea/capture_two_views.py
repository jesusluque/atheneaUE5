# Copyright (c) 2026 jesus luque.
# Two scene captures of a level drawn in the same frames, one at the framing
# distance and one at FAR times it (as capture_lod.py places them): whether
# each view gets its own levels-of-detail cut. Exported NAME_near and NAME_far.
#   py capture_two_views.py LEVEL NAME FAR [CVAR=VALUE;...]
import unreal, os, sys, time

level, name, far = sys.argv[1], sys.argv[2], float(sys.argv[3])
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(level)
for command in (sys.argv[4] if len(sys.argv) > 4 else "").split(";"):
    if command.strip():
        unreal.SystemLibrary.execute_console_command(None, command.strip().replace("=", " "))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
for sky in [a for a in actors if isinstance(a, unreal.SkyLight)]:
    light = sky.get_editor_property("light_component")
    light.set_editor_property("source_cubemap_angle", 150.0)
    light.recapture_sky()
splat = [a for a in actors if isinstance(a, unreal.GaussianSplatActor)][0]
origin, extent, _ = unreal.SystemLibrary.get_component_bounds(splat.get_editor_property("gaussian_splat_component"))
r = max(extent.x, extent.y, extent.z)
views = []
for label, dist in (("near", 1.0), ("far", far)):
    eye = origin + unreal.Vector(-2.8 * r * dist, 0.0, 0.6 * r * dist)
    rt = unreal.RenderingLibrary.create_render_target2d(world, 768, 768, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, eye, unreal.MathLibrary.find_look_at_rotation(eye, origin))
    cc = cap.get_editor_property("capture_component2d")
    cc.set_editor_property("fov_angle", 45.0)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_every_frame", True)
    pp = cc.get_editor_property("post_process_settings")
    pp.set_editor_property("override_vignette_intensity", True)
    pp.set_editor_property("vignette_intensity", 0.0)
    cc.set_editor_property("post_process_settings", pp)
    views.append((label, rt))

start = time.time()
state = {"done": 0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    if state["done"] == 0 and e >= 44:
        for label, rt in views:
            unreal.RenderingLibrary.export_render_target(world, rt, out, "%s_%s" % (name, label))
        unreal.log_warning("athenea: exported %s near and far" % name)
        state["done"] = 1
    elif state["done"] == 1 and e >= 48:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
