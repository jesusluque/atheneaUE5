# Copyright (c) 2026 jesus luque.
# A Movie Render Queue test for a level with a splat actor: a level sequence
# (/Game/Mrq/Seq_<name>) with a cine camera that turns once round the cloud in
# FRAMES frames and a camera cut track, and a config (/Game/Mrq/Cfg_<name>):
# deferred pass, PNG, WARMUP warm-up frames; AA taa (the engine's TAA/TSR,
# SAMPLES temporal samples) or none (no temporal AA nor motion blur, SAMPLES
# spatial samples a frame), and taa-noblur / none-blur to tell the two apart.
#   py setup_mrq.py LEVEL NAME [FRAMES] [WARMUP] [SAMPLES] [AA] --quit
# Then render it from the command line (operations manual, Movie Render Queue):
#   UnrealEditor-Cmd TestProj.uproject LEVEL -game -LevelSequence=/Game/Mrq/Seq_NAME
#     -MoviePipelineConfig=/Game/Mrq/Cfg_NAME -RenderOffscreen -Unattended -NoTextureStreaming
import unreal, math, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
level, name = args[0], args[1]
frames = int(args[2]) if len(args) > 2 else 24
warmup = int(args[3]) if len(args) > 3 else 8
samples = int(args[4]) if len(args) > 4 else 1
mode = args[5] if len(args) > 5 else "taa"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
les.load_level(level)
splat = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.GaussianSplatActor)][0]
origin, extent, _ = unreal.SystemLibrary.get_component_bounds(splat.get_editor_property("gaussian_splat_component"))
r = max(extent.x, extent.y, extent.z)

# The sequence: a cine camera spawned in it, its transform keyed round the cloud.
seq_name = "Seq_" + name
existing = unreal.EditorAssetLibrary.does_asset_exist("/Game/Mrq/" + seq_name)
if existing:
    unreal.EditorAssetLibrary.delete_asset("/Game/Mrq/" + seq_name)
seq = tools.create_asset(seq_name, "/Game/Mrq", unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
seq.set_display_rate(unreal.FrameRate(24, 1))
seq.set_playback_start(0)
seq.set_playback_end(frames)
cam = eas.spawn_actor_from_class(unreal.CineCameraActor, origin)
binding = seq.add_spawnable_from_instance(cam)
eas.destroy_actor(cam)
transform = binding.add_track(unreal.MovieScene3DTransformTrack)
section = transform.add_section()
section.set_range(0, frames)
channels = section.get_all_channels()   # location x y z, rotation x y z, scale x y z
for f in range(frames + 1):
    a = 2.0 * math.pi * f / frames
    eye = unreal.Vector(origin.x - 2.8 * r * math.cos(a), origin.y - 2.8 * r * math.sin(a), origin.z + 0.6 * r)
    rot = unreal.MathLibrary.find_look_at_rotation(eye, origin)
    t = unreal.FrameNumber(f)
    for c, v in zip(channels[:6], [eye.x, eye.y, eye.z, rot.roll, rot.pitch, rot.yaw]):
        c.add_key(t, v)
cuts = seq.add_track(unreal.MovieSceneCameraCutTrack)
cut = cuts.add_section()
cut.set_range(0, frames)
cut.set_camera_binding_id(seq.get_binding_id(binding))
unreal.EditorAssetLibrary.save_loaded_asset(seq)

# The config: deferred, PNG into ~/athenea-unreal/shots/mrq_<name>, warm-up frames.
cfg_name = "Cfg_" + name
if unreal.EditorAssetLibrary.does_asset_exist("/Game/Mrq/" + cfg_name):
    unreal.EditorAssetLibrary.delete_asset("/Game/Mrq/" + cfg_name)
cfg = tools.create_asset(cfg_name, "/Game/Mrq", unreal.MoviePipelinePrimaryConfig, None)
cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_PNG)
out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
out.output_directory = unreal.DirectoryPath("{project_dir}/../shots/mrq_" + name)
out.output_resolution = unreal.IntPoint(768, 768)
out.file_name_format = "{frame_number}"
aa = cfg.find_or_add_setting_by_class(unreal.MoviePipelineAntiAliasingSetting)
# none / none-blur: no temporal AA, spatial samples; taa / taa-noblur: the
# engine's own. Motion blur is off in none and taa-noblur.
if mode.startswith("none"):
    aa.override_anti_aliasing = True
    aa.anti_aliasing_method = unreal.AntiAliasingMethod.AAM_NONE
    aa.spatial_sample_count = samples
else:
    aa.override_anti_aliasing = False
    aa.temporal_sample_count = samples
if mode in ("none", "taa-noblur"):
    cvars = cfg.find_or_add_setting_by_class(unreal.MoviePipelineConsoleVariableSetting)
    cvars.add_or_update_console_variable("r.MotionBlurQuality", 0.0)
aa.engine_warm_up_count = warmup
aa.render_warm_up_count = warmup
unreal.EditorAssetLibrary.save_loaded_asset(cfg)
unreal.log_warning("athenea: %s and %s made for %s, %d frames, %d warm-up, %d samples, AA %s" % (seq_name, cfg_name, level, frames, warmup, samples, mode))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
