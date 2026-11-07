# Copyright (c) 2026 jesus luque.
# Opens /Game/Test, frames the splat actor, waits for shaders, takes a shot, quits.
import unreal, os

out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
os.makedirs(out, exist_ok=True)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/Test")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
splat = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)][0]
splat.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
comp = splat.get_editor_property("gaussian_splat_component")
origin, extent, _ = unreal.SystemLibrary.get_component_bounds(comp)
unreal.log_warning("athenea: bounds origin %s extent %s" % (origin, extent))
r = max(extent.x, extent.y, extent.z)
import math
views = [("front", 0.0, 12.0), ("side", 90.0, 12.0), ("high", 45.0, 40.0)]
cams = []
for name, yaw, pitch in views:
    yr, pr = math.radians(yaw), math.radians(pitch)
    d = 12.0 * r
    offset = unreal.Vector(-d * math.cos(pr) * math.cos(yr), -d * math.cos(pr) * math.sin(yr), d * math.sin(pr))
    cam = eas.spawn_actor_from_class(unreal.CameraActor, origin + offset)
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(), origin), False)
    cam.camera_component.set_editor_property("field_of_view", 30.0)
    cams.append((name, cam))

import time, json
with open(os.path.join(out, "cameras.json"), "w") as f:
    json.dump({n: {"eye": [c.get_actor_location().x, c.get_actor_location().y, c.get_actor_location().z],
                   "target": [origin.x, origin.y, origin.z], "fov": 30.0} for n, c in cams}, f, indent=1)
start = time.time()
state = {"next": 0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["next"]
    if i < len(cams) and e >= 60 + 10 * i:
        name, cam = cams[i]
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, os.path.join(out, "pawn_sh_%s.png" % name), cam)
        unreal.log_warning("athenea: shot %s" % name)
        state["next"] = i + 1
    if e >= 60 + 10 * len(cams) + 15:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
