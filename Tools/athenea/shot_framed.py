# Copyright (c) 2026 jesus luque.
# A shot of each level given, framed on its splat component's bounds, from a
# side named before the levels (default -x; +y is the front of a y-up,
# right-handed file such as an SPZ or athenea's):
#   py shot_framed.py [-x|+x|-y|+y] [x<distance factor>] /Game/A /Game/B
import unreal, os, sys, time, math

args = sys.argv[1:]
side = args.pop(0) if args and args[0] in ("-x", "+x", "-y", "+y") else "-x"
# An optional distance factor, "x<k>": the camera k times further than the framing.
far = float(args.pop(0)[1:]) if args and args[0].startswith("x") else 1.0
levels = args
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
state = {"i": 0, "at": None, "cam": None, "phase": 0}
start = time.time()

def frame(level):
    les.load_level(level)
    splat = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)][0]
    origin, extent, _ = unreal.SystemLibrary.get_component_bounds(splat.get_editor_property("gaussian_splat_component"))
    r = max(extent.x, extent.y, extent.z)
    d = {"-x": (-1, 0), "+x": (1, 0), "-y": (0, -1), "+y": (0, 1)}[side]
    eye = origin + unreal.Vector(2.8 * far * r * d[0], 2.8 * far * r * d[1], 0.6 * far * r)
    cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, origin), False)
    cam.camera_component.set_editor_property("field_of_view", 45.0)
    unreal.log_warning("athenea: %s bounds %s extent %s" % (level, origin, extent))
    return cam

def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["i"]
    if i >= len(levels):
        if e >= state["at"] + 8:
            unreal.SystemLibrary.quit_editor()
        return
    if state["phase"] == 0 and e >= (40 if i == 0 else state["at"] + 4):
        state["cam"] = frame(levels[i])
        state["phase"] = 1
        state["at"] = e
    elif state["phase"] == 1 and e >= state["at"] + 12:
        name = levels[i].split("/")[-1]
        unreal.AutomationLibrary.take_high_res_screenshot(1024, 768, os.path.join(out, "framed_%s%s.png" % (name, side.replace("+", "p").replace("-", "m"))), state["cam"])
        unreal.log_warning("athenea: shot %s" % name)
        state["phase"] = 0
        state["i"] = i + 1
        state["at"] = e
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
