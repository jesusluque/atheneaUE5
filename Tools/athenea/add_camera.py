# Copyright (c) 2026 jesus luque.
# A player camera in a level, for -game (ue_time.sh): at EYE looking at TARGET
# (centimetres, UE's axes), FOV degrees horizontally.
#   py add_camera.py LEVEL EX EY EZ TX TY TZ [FOV] [--quit]
import unreal, sys
a = [x for x in sys.argv[1:] if not x.startswith("--")]
level = a[0]
eye = unreal.Vector(float(a[1]), float(a[2]), float(a[3]))
target = unreal.Vector(float(a[4]), float(a[5]), float(a[6]))
fov = float(a[7]) if len(a) > 7 else 60.0
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(level)
for x in eas.get_all_level_actors():
    if isinstance(x, unreal.CameraActor) and x.get_actor_label().startswith("BenchCamera"):
        eas.destroy_actor(x)
cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
cam.set_actor_label("BenchCamera")
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target), False)
cam.camera_component.set_editor_property("field_of_view", fov)
cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: camera in %s at %s looking at %s" % (level, eye, target))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
