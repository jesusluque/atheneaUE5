# Copyright (c) 2026 jesus luque.
# HDR and PNG shots of /Game/TestTx from its camera, the sky light turned by
# each angle given:  py shot_tx.py 0 90 180 270
import unreal, os, sys, time

angles = [float(a) for a in sys.argv[1:]] or [0.0]
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/TestTx")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
cam = [a for a in actors if isinstance(a, unreal.CameraActor)][0]
light = [a for a in actors if isinstance(a, unreal.SkyLight)][0].get_editor_property("light_component")

start = time.time()
state = {"i": 0, "set": -1}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    i = state["i"]
    if i < len(angles):
        if state["set"] != i:
            light.set_editor_property("source_cubemap_angle", angles[i])
            light.recapture_sky()
            state["set"] = i
            state["at"] = e
        # One screenshot request a few seconds: two in one tick replace each other.
        name = os.path.join(out, "tx_ue_%03d" % int(angles[i]))
        if e >= max(60, state["at"] + 8) and not state.get("hdr"):
            unreal.AutomationLibrary.take_high_res_screenshot(1024, 1024, name + ".exr", cam, capture_hdr=True)
            state["hdr"] = e
        elif state.get("hdr") and e >= state["hdr"] + 4:
            unreal.AutomationLibrary.take_high_res_screenshot(1024, 1024, name + ".png", cam)
            unreal.log_warning("athenea: shot %s" % name)
            state["hdr"] = None
            state["i"] = i + 1
    elif e >= state.get("at", 0) + 25:
        unreal.SystemLibrary.quit_editor()
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
