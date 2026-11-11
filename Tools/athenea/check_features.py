# Copyright (c) 2026 jesus luque.
# Frames of a level's splat actor for the features athenea brought in 1.3:
# framed on its bounds from -x and a little above, tone mapped to PNG.
#   py check_features.py LEVEL NAME MODE
# MODE:
#   plain                  one frame
#   skin:T1/T2/...         one frame a time (seconds into the cloud's poses)
#   edit:MODE              a box over the cloud's +x half (MODE grade, keep or
#                          remove), graded red where kept, then one frame
#   lamps:GROUP=R/G/B;...  the lamp groups' weights, then one frame
#   cvar:NAME=VALUE;...    console variables set, then one frame
#   flight:N/SECONDS       N frames evenly over the cloud's poses
#   relight:N/T            the sky light turned once round in N steps, the cloud at T s
#   sky:FILE.hdr+MODE      a sky light from the HDR (in ~/athenea-unreal, Z up,
#                          turned by its yaw: sky:FILE.hdr@DEG), then MODE
# The capture's tone curve is off and its exposure fixed at 0 EV, so a frame
# is the scene's light through the sRGB curve, as athenea writes one.
# Lists are separated by "/": the editor's -ExecCmds splits its argument at
# commas.
import unreal, os, sys, time

level, name, mode = sys.argv[1], sys.argv[2], sys.argv[3]
out = os.path.join(os.path.expanduser("~"), "athenea-unreal", "shots", "f13")
os.makedirs(out, exist_ok=True)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
# The splats' shadow material: its blend mode and clip value, logged.
try:
    m = unreal.load_asset("/StarLightGS/Materials/M_SplatShadowMaterial")
    unreal.log_warning("athenea: shadow material blend %s clip %s" % (m.get_editor_property("blend_mode"), m.get_editor_property("opacity_mask_clip_value")))
except Exception as e:
    unreal.log_warning("athenea: shadow material not read: %s" % e)
les.load_level(level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()
splats = [a.get_editor_property("gaussian_splat_component") for a in actors if isinstance(a, unreal.GaussianSplatActor)]

lo, hi = None, None
for c in splats:
    o, x, _ = unreal.SystemLibrary.get_component_bounds(c)
    a, b = o - x, o + x
    lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
    hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
origin, extent = (lo + hi) * 0.5, (hi - lo) * 0.5
r = max(extent.x, extent.y, extent.z)
eye = origin + unreal.Vector(-2.8 * r, 0.0, 0.6 * r)
rt = unreal.RenderingLibrary.create_render_target2d(world, 1024, 1024, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, eye, unreal.MathLibrary.find_look_at_rotation(eye, origin))
cc = cap.get_editor_property("capture_component2d")
cc.set_editor_property("fov_angle", 45.0)
cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_TONE_CURVE_HDR)
cc.set_editor_property("texture_target", rt)
cc.set_editor_property("capture_every_frame", True)
unreal.log_warning("athenea: %s bounds %s extent %s" % (level, origin, extent))

# A sky light from an HDR, as the plugin's TX clouds read it.
if mode.startswith("sky:"):
    sky, _, mode = mode[4:].partition("+")
    hdr, _, yaw = sky.partition("@")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(os.path.expanduser("~"), "athenea-unreal", hdr))
    task.set_editor_property("destination_path", "/Game/Sky")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    cube = unreal.load_asset("/Game/Sky/" + os.path.splitext(hdr)[0])
    # The level's own sky light (setup_files.py gives every level the studio
    # one), else a new one: a scene draws with one sky light only.
    lights = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.SkyLight)]
    light = lights[0] if lights else eas.spawn_actor_from_class(unreal.SkyLight, origin)
    lc = light.get_editor_property("light_component")
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    lc.set_editor_property("cubemap", cube)
    lc.set_editor_property("source_cubemap_angle", float(yaw or 0.0))
    lc.recapture_sky()
    unreal.log_warning("athenea: sky %s (%s) yaw %s" % (hdr, cube, yaw or 0))
    name = "%s_sky%s" % (name, yaw or "0")
# A sun: the level's directional light (else a new one) turned to PITCH/YAW
# degrees, at LUX, and the sky light dimmed to a tenth so the sun reads.
if mode.startswith("sun:"):
    sun, _, mode = mode[4:].partition("+")
    angles = [float(v) for v in sun.split("/")]
    pitch, yaw = angles[0], angles[1] if len(angles) > 1 else 0.0
    lux = angles[2] if len(angles) > 2 else 3.0
    suns = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.DirectionalLight)]
    light = suns[0] if suns else eas.spawn_actor_from_class(unreal.DirectionalLight, origin)
    light.set_actor_rotation(unreal.Rotator(0.0, -pitch, yaw), False)
    dc = light.get_editor_property("light_component")
    dc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    dc.set_editor_property("intensity", lux)
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.SkyLight):
            a.get_editor_property("light_component").set_editor_property("intensity", 0.1)
    unreal.log_warning("athenea: sun pitch %s yaw %s at %s lux" % (pitch, yaw, lux))
    name = "%s_sun%d_%d" % (name, int(pitch), int(yaw))
# Console variables set before the shots: set:NAME=VALUE;...+MODE
if mode.startswith("set:"):
    sets, _, mode = mode[4:].partition("+")
    for one in sets.split(";"):
        n, _, v = one.partition("=")
        unreal.SystemLibrary.execute_console_command(world, "%s %s" % (n, v))
    name = "%s_%s" % (name, sets.split(";")[0].split(".")[-1].replace("=", ""))
pp = cc.get_editor_property("post_process_settings")
for k, v in (("override_tone_curve_amount", True), ("tone_curve_amount", 0.0),
             ("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
             ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0),
             ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False)):
    try:
        pp.set_editor_property(k, v)
    except Exception as x:
        unreal.log_warning("athenea: post process %s not set: %s" % (k, x))
cc.set_editor_property("post_process_settings", pp)
cc.set_editor_property("post_process_blend_weight", 1.0)

shots = []   # (label, setter)
kind, _, arg = mode.partition(":")
if kind == "skin":
    for t in [float(v) for v in arg.replace(",", "/").split("/")]:
        def at(t=t):
            for c in splats:
                c.set_editor_property("skin_playing", False)
                c.set_skin_time(t)
        shots.append(("t%05.2f" % t, at))
elif kind == "edit":
    def edit():
        for c in splats:
            e = c.get_editor_property("edit")
            e.set_editor_property("active", True)
            e.set_editor_property("mode", {"grade": unreal.GSSplatEditMode.GRADE, "keep": unreal.GSSplatEditMode.KEEP,
                                           "remove": unreal.GSSplatEditMode.REMOVE}[arg or "grade"])
            # The component's local space: the box over the +x half of its bounds.
            local = c.get_world_transform().inverse_transform_location(origin)
            e.set_editor_property("centre", unreal.Vector3f(local.x + extent.x * 0.5, local.y, local.z))
            e.set_editor_property("size", unreal.Vector3f(extent.x * 0.5, extent.y * 2.0, extent.z * 2.0))
            e.set_editor_property("tint", unreal.LinearColor(1.0, 0.2, 0.2, 1.0))
            e.set_editor_property("saturation", 1.0)
            e.set_editor_property("brightness", 1.5)
            # set_editor_property re-registers the component (PreEditChange /
            # PostEditChange), so the proxy is rebuilt with the edit.
            c.set_editor_property("edit", e)
    shots.append((arg or "grade", edit))
elif kind == "lamps":
    def lamps():
        for c in splats:
            for g in arg.split(";"):
                n, _, v = g.partition("=")
                rgb = [float(x) for x in v.replace(",", "/").split("/")]
                c.set_light_group(n, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), 1.0, True)
    shots.append(("lamps", lamps))
elif kind == "flight":
    # N frames evenly over SECONDS of the cloud's poses: flight:N/SECONDS
    n, _, span = arg.replace(",", "/").partition("/")
    n, span = int(n), float(span or "2")
    for i in range(n):
        def at(t=span * i / max(n - 1, 1)):
            for c in splats:
                c.set_editor_property("skin_playing", False)
                c.set_skin_time(t)
        shots.append(("f%03d" % i, at))
elif kind == "relight":
    # The level's sky light turned once round in N steps from its yaw, the
    # cloud held at T seconds: relight:N/T
    n, _, t = arg.replace(",", "/").partition("/")
    n, t = int(n), float(t or "0")
    lights = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.SkyLight)]
    base = lights[0].get_editor_property("light_component").get_editor_property("source_cubemap_angle") if lights else 0.0
    for i in range(n):
        def turn(yaw=base + 360.0 * i / n):
            for c in splats:
                c.set_editor_property("skin_playing", False)
                c.set_skin_time(t)
            for l in lights:
                lc = l.get_editor_property("light_component")
                lc.set_editor_property("source_cubemap_angle", yaw % 360.0)
                lc.recapture_sky()
        shots.append(("r%03d" % i, turn))
elif kind == "cvar":
    def cvar():
        for one in arg.split(";"):
            n, _, v = one.partition("=")
            unreal.SystemLibrary.execute_console_command(world, "%s %s" % (n, v))
    shots.append((arg.split(";")[0].split(".")[-1].replace("=", ""), cvar))
else:
    shots.append(("plain", lambda: None))

start = time.time()
state = {"i": 0, "phase": 0, "at": 40.0}
def tick(dt):
    if getattr(unreal, "_athenea_owner", None) is not tick:
        return
    e = time.time() - start
    if e < state["at"]:
        # The renderer draws this scene only when it is captured (an editor
        # with no viewport): kept drawing while waiting, so the sky light's
        # capture, the textures' streaming and the readbacks have frames.
        cc.capture_scene()
        return
    i = state["i"]
    if i >= len(shots):
        unreal.SystemLibrary.quit_editor()
        return
    label, setter = shots[i]
    if state["phase"] == 0:
        try:
            setter()
        except Exception as x:
            # A shot that cannot be set is logged and skipped, not retried
            # every tick.
            unreal.log_error("athenea: %s_%s not set: %s" % (name, label, x))
            state["i"], state["at"] = i + 1, e + 0.5
            return
        state["phase"], state["at"] = 1, e + 2.0
    else:
        cc.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, out, "%s_%s.png" % (name, label))
        unreal.log_warning("athenea: %s_%s" % (name, label))
        state["i"], state["phase"], state["at"] = i + 1, 0, e + 0.5
unreal._athenea_owner = tick
unreal.register_slate_post_tick_callback(tick)
