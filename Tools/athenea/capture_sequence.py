# Copyright (c) 2026 jesus luque.
# Frames of a sequence level (setup_sequence.py), each through a SceneCapture2D
# into a float render target exported as EXR, the camera framed on all the
# frames' bounds from -x and a little above:
#   py capture_sequence.py LEVEL NAME FRAME[,FRAME...] [CVAR=VALUE;...]  (frames also one an argument)
import unreal, os, sys, time

level, name = sys.argv[1], sys.argv[2]
# The frames: one argument with commas, or one an argument (PowerShell turns
# a list with commas into separate ones); a console variable has an "=".
wanted = [int(f) for a in sys.argv[3:] if "=" not in a and a != "profile" for f in a.split(",") if f.strip()]
# "profile": UE's GPU profile of a frame (ProfileGPU) into the log, after the shots.
profile = "profile" in sys.argv[3:]
# "orbit=D": the camera turns D degrees about the bounds' vertical a frame
# shot; "size=N": N x N pixels (default 768). A backdrop is the level's own
# (setup_backdrop.py): UE's scene colour capture is taken before the splats
# are drawn, so there is no alpha to composite with.
flags = dict(a.split("=", 1) for a in sys.argv[3:] if a.startswith("orbit=") or a.startswith("size="))
orbit = float(flags.get("orbit", "0"))
size = int(flags.get("size", "768"))
cvars = ";".join(a for a in sys.argv[3:] if "=" in a and not a.startswith("orbit=") and not a.startswith("size="))
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(level)
for command in cvars.split(";"):
    if command.strip():
        unreal.SystemLibrary.execute_console_command(None, command.strip().replace("=", " "))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)][0]
comp = actor.get_editor_property("gaussian_splat_component")
comp.set_editor_property("sequence_playing", False)
fps = comp.get_editor_property("sequence_frames_per_second")
count = len(comp.get_editor_property("sequence_frames"))
# Framed on the union of every frame's bounds, so that the whole sequence
# stays in view.
lo, hi = None, None
for k in range(count):
    comp.set_sequence_time((k + 0.5) / fps)
    o, x, _ = unreal.SystemLibrary.get_component_bounds(comp)
    a, b = o - x, o + x
    lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
    hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
origin, extent = (lo + hi) * 0.5, (hi - lo) * 0.5
r = max(extent.x, extent.y, extent.z)
eye = origin + unreal.Vector(-2.6 * r, 0.0, 0.5 * r)
rt = unreal.RenderingLibrary.create_render_target2d(world, size, size, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
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
unreal.log_warning("athenea: %s, %d frames, bounds %s extent %s" % (level, count, origin, extent))

start = time.time()
state = {"i": 0, "at": 40.0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["i"]
    if i >= len(wanted):
        if profile and not state.get("profiled"):
            unreal.SystemLibrary.execute_console_command(None, "ProfileGPU")
            state["profiled"] = True
            state["at"] = e + 1.0
            return
        if e >= state["at"] + 3:
            unreal.SystemLibrary.quit_editor()
        return
    if e < state["at"]:
        return
    if e < state["at"] + 0.2:
        comp.set_sequence_time((wanted[i] + 0.5) / fps)
        if orbit != 0.0:
            a = unreal.MathLibrary.degrees_to_radians(orbit * i)
            off = unreal.Vector(-2.6 * r * unreal.MathLibrary.cos(a), -2.6 * r * unreal.MathLibrary.sin(a), 0.5 * r)
            cap.set_actor_location(origin + off, False, False)
            cap.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(origin + off, origin), False)
        return
    if e < state["at"] + 3.0:
        cc.capture_scene()
        return
    unreal.RenderingLibrary.export_render_target(world, rt, out, "%s_f%03d" % (name, wanted[i]))
    unreal.log_warning("athenea: exported %s frame %d" % (name, wanted[i]))
    state["i"] = i + 1
    state["at"] = e + 0.5
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
