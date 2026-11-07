# Copyright (c) 2026 jesus luque.
# Puts a camera that the player takes on start at the comparison's front
# view, so -game renders exactly that frame.
import unreal, os, json

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/Test")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.CameraActor):
        eas.destroy_actor(a)
splat = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)][0]
splat.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
cam = json.load(open(os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots", "cameras.json")))["front"]
eye = unreal.Vector(*cam["eye"])
c = eas.spawn_actor_from_class(unreal.CameraActor, eye)
c.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(*cam["target"])), False)
c.camera_component.set_editor_property("field_of_view", cam["fov"])
c.camera_component.set_editor_property("constrain_aspect_ratio", False)
c.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
les.save_current_level()
unreal.log_warning("athenea: game camera saved")
