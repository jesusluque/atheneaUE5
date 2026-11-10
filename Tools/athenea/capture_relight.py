# Copyright (c) 2026 jesus luque.
# A level's relit clouds under a light that turns: from the level's camera,
# FRAMES tone-mapped PNGs (NAME_0000 ...) while the sky light's cubemap turns
# once round and the directional light, if there is one, turns with it. A
# level with no camera is framed on its clouds.
#   py capture_relight.py LEVEL NAME FRAMES [WIDTH HEIGHT]
import unreal, os, sys, time, math

level, name, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
width = int(sys.argv[4]) if len(sys.argv) > 4 else 1280
height = int(sys.argv[5]) if len(sys.argv) > 5 else 720
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots", name)
os.makedirs(out, exist_ok=True)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
cams = [a for a in actors if isinstance(a, unreal.CameraActor)]
if cams:
    eye, look, fov = cams[0].get_actor_location(), cams[0].get_actor_rotation(), cams[0].camera_component.get_editor_property("field_of_view")
else:
    # No camera in the level: its clouds framed on their bounds.
    lo, hi = None, None
    for c in [a for a in actors if isinstance(a, unreal.GaussianSplatActor)]:
        o, x, _ = unreal.SystemLibrary.get_component_bounds(c.get_editor_property("gaussian_splat_component"))
        a, b = o - x, o + x
        lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
        hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
    origin = (lo + hi) * 0.5
    r = max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z) * 0.5
    y, el, fov = math.radians(35.0), math.radians(12.0), 40.0
    eye = origin + unreal.Vector(-5.5 * r * math.cos(el) * math.cos(y), -5.5 * r * math.cos(el) * math.sin(y), 5.5 * r * math.sin(el))
    look = unreal.MathLibrary.find_look_at_rotation(eye, origin)
skies = [a for a in actors if isinstance(a, unreal.SkyLight)]
suns = [a for a in actors if isinstance(a, unreal.DirectionalLight)]
sky = skies[0].get_editor_property("light_component") if skies else None
sky0 = sky.get_editor_property("source_cubemap_angle") if sky else 0.0
sun0 = suns[0].get_actor_rotation() if suns else None
unreal.log_warning("athenea: %d sky lights, %d directional lights" % (len(skies), len(suns)))

rt = unreal.RenderingLibrary.create_render_target2d(world, width, height, unreal.TextureRenderTargetFormat.RTF_RGBA8)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, eye, look)
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", fov)
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_TONE_CURVE_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)

start = time.time()
state = {"i": -1, "at": 45.0, "set": False}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    if e < state["at"]:
        return
    i = state["i"]
    if i >= frames:
        unreal.SystemLibrary.quit_editor()
        return
    if not state["set"]:
        # The next frame's light, then a moment for the sky to be captured
        # and the cloud's projection of it to come back.
        turn = 360.0 * (i + 1) / frames
        if sky:
            sky.set_editor_property("source_cubemap_angle", sky0 + turn)
            sky.recapture_sky()
        if sun0:
            suns[0].set_actor_rotation(unreal.Rotator(sun0.roll, sun0.pitch, sun0.yaw + turn), False)
        state["set"] = True
        state["at"] = e + 1.5
        return
    cc.capture_scene()
    if i >= 0:
        unreal.RenderingLibrary.export_render_target(world, rt, out, "%s_%04d.png" % (name, i))
    state["i"] = i + 1
    state["set"] = False
    state["at"] = e + 0.2
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
