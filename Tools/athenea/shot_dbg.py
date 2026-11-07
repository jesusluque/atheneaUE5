# Copyright (c) 2026 jesus luque.
# HDR shots of /Game/TestTx with r.Gaussians.Athenea.Debug set to each value
# given, the sky light at 150 degrees:  py shot_dbg.py 100 101 ...
import unreal, os, sys, time

values = [int(a) for a in sys.argv[1:]]
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/TestTx")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
cam = [a for a in actors if isinstance(a, unreal.CameraActor)][0]
light = [a for a in actors if isinstance(a, unreal.SkyLight)][0].get_editor_property("light_component")
light.set_editor_property("source_cubemap_angle", 150.0)
light.recapture_sky()

start = time.time()
state = {"i": 0, "at": 0.0, "set": -1}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["i"]
    if i >= len(values):
        if e >= state["at"] + 10:
            unreal.SystemLibrary.quit_editor()
        return
    if state["set"] != i:
        unreal.SystemLibrary.execute_console_command(None, "r.Gaussians.Athenea.Debug %d" % values[i])
        state["set"] = i
        state["at"] = e
    elif e >= max(60, state["at"] + 3):
        unreal.AutomationLibrary.take_high_res_screenshot(256, 256, os.path.join(out, "dbg_%d.exr" % values[i]), cam, capture_hdr=True)
        unreal.log_warning("athenea: debug %d" % values[i])
        state["i"] = i + 1
        state["set"] = -1
        state["at"] = e
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
