# Copyright (c) 2026 jesus luque.
# Turns a level's clouds over about X (roll 180, a rotation: nothing is
# mirrored) and lifts them back into the Z range they held, for captures whose
# up came out as UE's -Z; saves the level.
#   py flip_level.py LEVEL [--quit]
import unreal, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(args[0])
clouds = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)]


def zrange():
    lo, hi = 1e30, -1e30
    for a in clouds:
        o, x, _ = unreal.SystemLibrary.get_component_bounds(a.get_editor_property("gaussian_splat_component"))
        lo, hi = min(lo, o.z - x.z), max(hi, o.z + x.z)
    return lo, hi

lo, hi = zrange()
for a in clouds:
    r = a.get_actor_rotation()
    a.set_actor_rotation(unreal.Rotator(roll=r.roll + 180.0, pitch=r.pitch, yaw=r.yaw), False)
nlo, nhi = zrange()
for a in clouds:
    a.set_actor_location(a.get_actor_location() + unreal.Vector(0, 0, lo - nlo), False, False)
les.save_current_level()
unreal.log_warning("athenea: flipped %d clouds, z %.0f..%.0f" % (len(clouds), *zrange()))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
