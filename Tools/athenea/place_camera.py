# Copyright (c) 2026 jesus luque.
# A level's BenchCamera put at an explicit pose (centimetres, UE's axes, '/'
# separated: -ExecCmds splits at commas), FOV 60 across, then saved; the player
# camera the -game timings (ue_time.sh) are taken from.
#   py place_camera.py LEVEL EX/EY/EZ:TX/TY/TZ [--quit]
import unreal, sys
a = [x for x in sys.argv[1:] if not x.startswith("--")]
level = a[0]
e, t = a[1].split(":")
eye = unreal.Vector(*[float(x) for x in e.split("/")])
target = unreal.Vector(*[float(x) for x in t.split("/")])
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(level)
cams = [x for x in eas.get_all_level_actors() if x.get_actor_label() == "BenchCamera"]
cam = cams[0] if cams else eas.spawn_actor_from_class(unreal.CameraActor, eye)
cam.set_actor_label("BenchCamera")
cam.set_actor_location(eye, False, False)
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target), False)
cam.camera_component.set_editor_property("field_of_view", 60.0)
cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: BenchCamera of %s at %s" % (level, a[1]))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
