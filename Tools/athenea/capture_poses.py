# Copyright (c) 2026 jesus luque.
# Stills of a level from explicit poses (centimetres, UE's axes), one EXR each,
# NAME_<k>, 1280 x 720 at 60 degrees: for placing a camera path in a scene whose
# layout is not known. Steps, not time windows (a heavy scene's frames are slow).
#   py capture_poses.py LEVEL NAME EX/EY/EZ:TX/TY/TZ [...] [CVAR=VALUE;...]  (no commas: -ExecCmds splits at them)
import unreal, os, sys, time
level, name = sys.argv[1], sys.argv[2]
poses = []
cvars = []
for a in sys.argv[3:]:
    if "=" in a:
        cvars += [c for c in a.split(";") if c.strip()]
        continue
    e, t = a.split(":")
    poses.append((unreal.Vector(*[float(x) for x in e.split("/")]), unreal.Vector(*[float(x) for x in t.split("/")])))
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
for c in cvars:
    unreal.SystemLibrary.execute_console_command(None, c.replace("=", " "))
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rt = unreal.RenderingLibrary.create_render_target2d(world, 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, poses[0][0])
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", 60.0)
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
start = time.time()
state = {"i": 0, "step": 0, "at": 40.0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["i"]
    if i >= len(poses):
        if e >= state["at"] + 2:
            unreal.SystemLibrary.quit_editor()
        return
    if e < state["at"]:
        return
    if state["step"] == 0:
        eye, target = poses[i]
        cap.set_actor_location(eye, False, False)
        cap.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target), False)
        cc.capture_scene()
        state["step"], state["at"] = 1, e + 5.0
    elif state["step"] == 1:
        cc.capture_scene()
        state["step"], state["at"] = 2, e + 3.0
    else:
        unreal.RenderingLibrary.export_render_target(world, rt, out, "%s_%d" % (name, i))
        unreal.log_warning("athenea: exported %s_%d" % (name, i))
        state["i"], state["step"], state["at"] = i + 1, 0, e + 0.5
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
