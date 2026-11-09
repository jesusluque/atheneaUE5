# Copyright (c) 2026 jesus luque.
# capture_framed.py at a distance: the splat actor seen from DIST times its
# framing distance (2.8 times its radius, from -x and a little above), for the
# levels of detail (r.Gaussians.Athenea.LodPixels among the console variables).
#   py capture_lod.py LEVEL NAME DIST [CVAR=VALUE;...]
import unreal, os, sys, time

level = sys.argv[1]
name = sys.argv[2]
dist = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
angle = 150.0
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
    light.set_editor_property("source_cubemap_angle", angle)
    light.recapture_sky()
# Every splat actor (a scene's clouds), framed on their bounds together.
splats = [a for a in actors if isinstance(a, unreal.GaussianSplatActor)]
def bounds():
    lo, hi = None, None
    for s in splats:
        o, x, _ = unreal.SystemLibrary.get_component_bounds(s.get_editor_property("gaussian_splat_component"))
        a, b = o - x, o + x
        lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
        hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
    return (lo + hi) * 0.5, (hi - lo) * 0.5
origin, extent = bounds()
r = max(extent.x, extent.y, extent.z)
# A cloud a few centimetres across would put the camera inside the 10 cm near
# plane: it is scaled up to a metre about its middle (as capture_framed.py).
if r < 50.0:
    k = 50.0 / r
    for s in splats:
        s.set_actor_scale3d(s.get_actor_scale3d() * k)
        s.set_actor_location(origin + (s.get_actor_location() - origin) * k, False, False)
    origin, extent = bounds()
    r = max(extent.x, extent.y, extent.z)
eye = origin + unreal.Vector(-2.8 * r * dist, 0.0, 0.6 * r * dist)
rotation = unreal.MathLibrary.find_look_at_rotation(eye, origin)

rt = unreal.RenderingLibrary.create_render_target2d(world, 768, 768, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, eye, rotation)
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", 45.0)
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
pp = cc.get_editor_property("post_process_settings")
pp.set_editor_property("override_vignette_intensity", True)
pp.set_editor_property("vignette_intensity", 0.0)
cc.set_editor_property("post_process_settings", pp)
unreal.log_warning("athenea: %s bounds %s extent %s" % (level, origin, extent))

start = time.time()
state = {"done": 0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    if state["done"] == 0 and e >= 40:
        cc.capture_scene()
        if e >= 44:
            state["done"] = 1
    elif state["done"] == 1 and e >= 45:
        unreal.RenderingLibrary.export_render_target(world, rt, out, name)
        unreal.log_warning("athenea: exported %s" % name)
        state["done"] = 2
    elif state["done"] == 2 and e >= 50:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
