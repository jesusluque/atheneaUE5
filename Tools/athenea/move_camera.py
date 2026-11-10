# Copyright (c) 2026 jesus luque.
# A level's BenchCamera moved round its clouds: YAW degrees about the vertical
# and HEIGHT (times the clouds' radius) up, at DIST radii, then saved.
#   py move_camera.py LEVEL YAW HEIGHT DIST [--quit]
import unreal, sys
a = [x for x in sys.argv[1:] if not x.startswith("--")]
level, yaw, height, dist = a[0], float(a[1]), float(a[2]), float(a[3])
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(level)
actors = eas.get_all_level_actors()
lo, hi = None, None
for s in [x for x in actors if isinstance(x, unreal.GaussianSplatActor)]:
    o, e, _ = unreal.SystemLibrary.get_component_bounds(s.get_editor_property("gaussian_splat_component"))
    p, q = o - e, o + e
    lo = p if lo is None else unreal.Vector(min(lo.x, p.x), min(lo.y, p.y), min(lo.z, p.z))
    hi = q if hi is None else unreal.Vector(max(hi.x, q.x), max(hi.y, q.y), max(hi.z, q.z))
origin = (lo + hi) * 0.5
r = max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z) * 0.5
t = unreal.MathLibrary.degrees_to_radians(yaw)
eye = origin + unreal.Vector(-dist * r * unreal.MathLibrary.cos(t), -dist * r * unreal.MathLibrary.sin(t), height * r)
cam = [x for x in actors if isinstance(x, unreal.CameraActor) and x.get_actor_label().startswith("BenchCamera")][0]
cam.set_actor_location(eye, False, False)
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, origin), False)
les.save_current_level()
unreal.log_warning("athenea: camera of %s at yaw %g height %g" % (level, yaw, height))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
