# Copyright (c) 2026 jesus luque.
# A camera flight through a level, for a video whose frame rate is measured:
# the level is copied to LEVEL_fly with an atmosphere and a sun (unshadowed: a
# shadow map of a hundred million splats is not the flight's subject), the
# image made athenea's (a post-process volume, below), a level
# sequence (/Game/Mrq/Seq_NAME) whose cine camera follows a Catmull-Rom path
# through the given poses, eased in and out, and a sequence actor that plays it
# when the level begins (-game, for the CSV profiler); and a Movie Render Queue
# config (/Game/Mrq/Cfg_NAME): deferred, PNG at RESX x RESY, no AA, no motion
# blur, into shots/mrq_NAME.
#   py fly_sequence.py LEVEL NAME SECONDS RESX RESY EX/EY/EZ:TX/TY/TZ [...] [CVAR=VALUE;...] [--quit]
# The console variables go into the MRQ config (default: LodPixels 2, MaxSplats
# 12 M); give the -game run the same ones.
# Then:
#   timing  UnrealEditor LEVEL_fly -game -benchmark -fps=30 (fixed steps: CSV frame k is sequence frame k)
#   frames  UnrealEditor-Cmd ... LEVEL_fly -game -LevelSequence=/Game/Mrq/Seq_NAME -MoviePipelineConfig=/Game/Mrq/Cfg_NAME
import unreal, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
level, name, seconds, resx, resy = args[0], args[1], float(args[2]), int(args[3]), int(args[4])
poses = []
cvars = {"r.Gaussians.Athenea.LodPixels": 2.0, "r.Gaussians.MaxSplats": 12000000.0}
for a in args[5:]:
    if "=" in a:
        for c in [c for c in a.split(";") if c.strip()]:
            k, v = c.split("=")
            cvars[k] = float(v)
        continue
    e, t = a.split(":")
    poses.append(([float(x) for x in e.split("/")], [float(x) for x in t.split("/")]))
fps = 30
frames = int(round(seconds * fps))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
fly = level + "_fly"
# Built on the level as loaded and saved under the new name (Save As): a map
# duplicated as an asset stays loaded, and loading it then fails.
les.load_level(level)

# The sky: an atmosphere lit by a sun the splats do not shadow.
sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 0))
sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-30.0, yaw=60.0), False)
sc = sun.get_editor_property("light_component")
sc.set_editor_property("intensity", 3.0)
sc.set_editor_property("cast_shadows", False)
sc.set_editor_property("atmosphere_sun_light", True)
sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))

# The image as athenea's: no filmic curve (the tone curve at 0), no vignette,
# gamut or blue correction, and the exposure as it is (bias 0): measured, the
# street frame's linear mean is athenea's at 0 EV (a PNG read with its alpha
# unassociated: MRQ's alpha is not coverage, and premultiplying by it had
# made the frame look 1.8 times darker than it is).
ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
ppv.set_editor_property("unbound", True)
pp = ppv.get_editor_property("settings")
for flag, prop, value in (("override_tone_curve_amount", "tone_curve_amount", 0.0),
                          ("override_auto_exposure_bias", "auto_exposure_bias", 0.0),
                          ("override_vignette_intensity", "vignette_intensity", 0.0),
                          ("override_expand_gamut", "expand_gamut", 0.0),
                          ("override_blue_correction", "blue_correction", 0.0)):
    pp.set_editor_property(flag, True)
    pp.set_editor_property(prop, value)
ppv.set_editor_property("settings", pp)


def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return [0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
            for a, b, c, d in zip(p0, p1, p2, p3)]


def along(which, s):
    pts = [p[which] for p in poses]
    pts = [pts[0]] + pts + [pts[-1]]
    n = len(poses) - 1
    u = min(s * n, n - 1e-6)
    i = int(u)
    return catmull(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], u - i)


seq_name = "Seq_" + name
if unreal.EditorAssetLibrary.does_asset_exist("/Game/Mrq/" + seq_name):
    unreal.EditorAssetLibrary.delete_asset("/Game/Mrq/" + seq_name)
seq = tools.create_asset(seq_name, "/Game/Mrq", unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
seq.set_display_rate(unreal.FrameRate(fps, 1))
seq.set_playback_start(0)
seq.set_playback_end(frames)
cam = eas.spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(*poses[0][0]))
cc = cam.get_cine_camera_component()
fb = cc.get_editor_property("filmback")
fb.sensor_width, fb.sensor_height = 36.0, 36.0 * resy / resx
cc.set_editor_property("filmback", fb)
cc.set_editor_property("current_focal_length", 31.18)   # 60 degrees across, as the probes
focus = cc.get_editor_property("focus_settings")
focus.focus_method = unreal.CameraFocusMethod.DISABLE
cc.set_editor_property("focus_settings", focus)
binding = seq.add_spawnable_from_instance(cam)
eas.destroy_actor(cam)
section = binding.add_track(unreal.MovieScene3DTransformTrack).add_section()
section.set_range(0, frames)
channels = section.get_all_channels()
prev_yaw = None
for f in range(frames + 1):
    x = f / frames
    s = x * x * (3 - 2 * x)
    eye, target = along(0, s), along(1, s)
    rot = unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*eye), unreal.Vector(*target))
    yaw = rot.yaw
    if prev_yaw is not None:
        while yaw - prev_yaw > 180: yaw -= 360
        while yaw - prev_yaw < -180: yaw += 360
    prev_yaw = yaw
    for c, v in zip(channels[:6], eye + [rot.roll, rot.pitch, yaw]):
        c.add_key(unreal.FrameNumber(f), v, interpolation=unreal.MovieSceneKeyInterpolation.LINEAR)
cut = seq.add_track(unreal.MovieSceneCameraCutTrack).add_section()
cut.set_range(0, frames)
cut.set_camera_binding_id(seq.get_binding_id(binding))
unreal.EditorAssetLibrary.save_loaded_asset(seq)

player = eas.spawn_actor_from_class(unreal.LevelSequenceActor, unreal.Vector(0, 0, 0))
player.set_sequence(seq)
ps = player.get_editor_property("playback_settings")
ps.auto_play = True
player.set_editor_property("playback_settings", ps)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.EditorLoadingAndSavingUtils.save_map(world, fly)

cfg_name = "Cfg_" + name
if unreal.EditorAssetLibrary.does_asset_exist("/Game/Mrq/" + cfg_name):
    unreal.EditorAssetLibrary.delete_asset("/Game/Mrq/" + cfg_name)
cfg = tools.create_asset(cfg_name, "/Game/Mrq", unreal.MoviePipelinePrimaryConfig, None)
cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_PNG)
out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
out.output_directory = unreal.DirectoryPath("{project_dir}/../shots/mrq_" + name)
out.output_resolution = unreal.IntPoint(resx, resy)
out.file_name_format = "{frame_number}"
aa = cfg.find_or_add_setting_by_class(unreal.MoviePipelineAntiAliasingSetting)
aa.override_anti_aliasing = True
aa.anti_aliasing_method = unreal.AntiAliasingMethod.AAM_NONE
aa.spatial_sample_count = 1
aa.engine_warm_up_count = 8
aa.render_warm_up_count = 8
cv = cfg.find_or_add_setting_by_class(unreal.MoviePipelineConsoleVariableSetting)
cv.add_or_update_console_variable("r.MotionBlurQuality", 0.0)
for k, v in cvars.items():
    cv.add_or_update_console_variable(k, v)
unreal.EditorAssetLibrary.save_loaded_asset(cfg)
unreal.log_warning("athenea: flight %s in %s, %d frames, %d poses" % (seq_name, fly, frames, len(poses)))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
