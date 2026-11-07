# Copyright (c) 2026 jesus luque.
# Shots of a level from the comparison's saved cameras (shots/cameras.json):
#   py shot_level.py /Game/Level name
import unreal, os, sys, json, time

level, name = sys.argv[1], sys.argv[2]
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
cams = json.load(open(os.path.join(out, "cameras.json")))
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(level)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = []
for view, c in cams.items():
    eye = unreal.Vector(*c["eye"])
    cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(*c["target"])), False)
    cam.camera_component.set_editor_property("field_of_view", c["fov"])
    actors.append((view, cam))

start = time.time()
state = {"next": 0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["next"]
    if i < len(actors) and e >= 60 + 10 * i:
        view, cam = actors[i]
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, os.path.join(out, "%s_%s.png" % (name, view)), cam)
        unreal.log_warning("athenea: shot %s %s" % (name, view))
        state["next"] = i + 1
    if e >= 60 + 10 * len(actors) + 15:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
