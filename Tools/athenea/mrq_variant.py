# Copyright (c) 2026 jesus luque.
# An MRQ config for a stretch of an existing sequence with console variables of
# its own, to tell apart what changes a few frames: /Game/Mrq/Cfg_NAME, PNG at
# RESX x RESY into shots/mrq_NAME, frames FIRST..LAST, no AA, no motion blur.
#   py mrq_variant.py NAME RESX RESY FIRST LAST [CVAR=VALUE;...] [--quit]
# mrq.AA=1 among the variables renders with the engine's temporal AA (TSR).
# mrq.Rate=N renders at N frames a second instead of the sequence's own: a
# multiple of it is the same camera move in slow motion, where a frame differs
# from its neighbours by what the renderer switches and little else.
import unreal, sys
a = [x for x in sys.argv[1:] if not x.startswith("--")]
name, resx, resy, first, last = a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4])
cvars = {}
for c in (a[5].split(";") if len(a) > 5 else []):
    if c.strip():
        k, v = c.split("="); cvars[k] = float(v)
tools = unreal.AssetToolsHelpers.get_asset_tools()
path = "/Game/Mrq/Cfg_" + name
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
cfg = tools.create_asset("Cfg_" + name, "/Game/Mrq", unreal.MoviePipelinePrimaryConfig, None)
cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_PNG)
out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
out.output_directory = unreal.DirectoryPath("{project_dir}/../shots/mrq_" + name)
out.output_resolution = unreal.IntPoint(resx, resy)
out.file_name_format = "{frame_number}"
out.use_custom_playback_range = True
out.custom_start_frame = first
out.custom_end_frame = last + 1
rate = int(cvars.pop("mrq.Rate", 0.0))
if rate > 0:
    out.use_custom_frame_rate = True
    out.output_frame_rate = unreal.FrameRate(rate, 1)
aa = cfg.find_or_add_setting_by_class(unreal.MoviePipelineAntiAliasingSetting)
# "mrq.AA=1" among the variables: the engine's temporal AA (TSR) instead of none.
tsr = cvars.pop("mrq.AA", 0.0) != 0.0
aa.override_anti_aliasing = True
aa.anti_aliasing_method = unreal.AntiAliasingMethod.AAM_TSR if tsr else unreal.AntiAliasingMethod.AAM_NONE
aa.spatial_sample_count = 1
aa.engine_warm_up_count = 8
aa.render_warm_up_count = 8
cv = cfg.find_or_add_setting_by_class(unreal.MoviePipelineConsoleVariableSetting)
cv.add_or_update_console_variable("r.MotionBlurQuality", 0.0)
for k, v in cvars.items():
    cv.add_or_update_console_variable(k, v)
unreal.EditorAssetLibrary.save_loaded_asset(cfg)
unreal.log_warning("athenea: %s, frames %d..%d, %s" % (path, first, last, cvars))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
