# Copyright (c) 2026 jesus luque.
# A timing level from a check level: its splat actors framed as
# check_features.py frames them (from -x, 2.8 bounds radii back, 0.6 up) by a
# camera the player looks through, saved as /Game/T_<name> for ue_time.sh.
#   py timing_level.py /Game/F_name name [still]
# still: a skinned cloud held at its first pose (its skin pass not run each
# frame), to time the pass against the cloud playing.
import unreal, sys

level, name = sys.argv[1], sys.argv[2]
still = len(sys.argv) > 3 and sys.argv[3] == "still"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(level)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
splats = [a.get_editor_property("gaussian_splat_component") for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)]
lo, hi = None, None
for c in splats:
    o, x, _ = unreal.SystemLibrary.get_component_bounds(c)
    a, b = o - x, o + x
    lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
    hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
origin, extent = (lo + hi) * 0.5, (hi - lo) * 0.5
r = max(extent.x, extent.y, extent.z)
eye = origin + unreal.Vector(-2.8 * r, 0.0, 0.6 * r)
cam = eas.spawn_actor_from_class(unreal.CameraActor, eye, unreal.MathLibrary.find_look_at_rotation(eye, origin))
cam.get_editor_property("camera_component").set_editor_property("field_of_view", 45.0)
cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
if still:
    for c in splats:
        c.set_editor_property("skin_playing", False)
unreal.EditorLoadingAndSavingUtils.save_map(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), "/Game/T_" + name)
unreal.log_warning("athenea: timing level /Game/T_%s, eye %s" % (name, eye))
unreal.SystemLibrary.quit_editor()
