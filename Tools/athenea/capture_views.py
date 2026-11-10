# Copyright (c) 2026 jesus luque.
# A level's clouds seen from several directions on their bounds (yaw and
# elevation in degrees, at DIST radii), one EXR each, NAME_<k>; for a scene
# whose orientation or framing is not known yet.
#   py capture_views.py LEVEL NAME DIST YAW:ELEV [YAW:ELEV ...]
import unreal, os, sys, time, math
level, name, dist = sys.argv[1], sys.argv[2], float(sys.argv[3])
views = [tuple(float(x) for x in v.split(":")) for v in sys.argv[4:]]
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
splats = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)]
lo, hi = None, None
for s in splats:
    o, x, _ = unreal.SystemLibrary.get_component_bounds(s.get_editor_property("gaussian_splat_component"))
    a, b = o - x, o + x
    lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
    hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
origin = (lo + hi) * 0.5
r = max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z) * 0.5
unreal.log_warning("athenea: bounds %s to %s" % (lo, hi))
rt = unreal.RenderingLibrary.create_render_target2d(world, 1024, 768, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, origin)
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", 60.0)
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
start = time.time()
state = {"i": 0, "at": 40.0, "placed": False}
def tick(dt):
    # Steps, not time windows: a heavy scene's frames come seconds apart.
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["i"]
    if i >= len(views):
        if e >= state["at"] + 2:
            unreal.SystemLibrary.quit_editor()
        return
    if e < state["at"]:
        return
    yaw, elev = views[i]
    if not state["placed"]:
        y, el = math.radians(yaw), math.radians(elev)
        eye = origin + unreal.Vector(-dist * r * math.cos(el) * math.cos(y), -dist * r * math.cos(el) * math.sin(y), dist * r * math.sin(el))
        cap.set_actor_location(eye, False, False)
        cap.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, origin), False)
        cc.capture_scene()
        state["placed"] = True
        state["shot"] = e + 6.0
        state["asked"] = False
        return
    if e < state["shot"]:
        return
    if not state["asked"]:
        # Asked for once more where every frame's capture may not have come.
        cc.capture_scene()
        state["asked"] = True
        state["shot"] = e + 3.0
        return
    unreal.RenderingLibrary.export_render_target(world, rt, out, "%s_%d" % (name, i))
    unreal.log_warning("athenea: exported %s_%d (yaw %g, elevation %g) at %.0f s" % (name, i, yaw, elev, e))
    state["i"] = i + 1
    state["placed"] = False
    state["at"] = e + 0.5
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
