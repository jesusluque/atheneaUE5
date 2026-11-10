# Copyright (c) 2026 jesus luque.
# A sequence held at one of its own frames, for telling what a renderer changes
# from frame to frame by itself: /Game/Mrq/Seq_NAME is a copy of /Game/Mrq/Seq_SOURCE
# whose camera stays where SOURCE's is at FRAME, for FRAMES frames.
#   py hold_sequence.py SOURCE NAME FRAME FRAMES [--quit]
import unreal, sys
a = [x for x in sys.argv[1:] if not x.startswith("--")]
source, name, frame, frames = a[0], a[1], int(a[2]), int(a[3])
path = "/Game/Mrq/Seq_" + name
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
seq = unreal.EditorAssetLibrary.duplicate_asset("/Game/Mrq/Seq_" + source, path)
held = 0
for binding in list(seq.get_bindings()) + list(seq.get_spawnables() if hasattr(seq, "get_spawnables") else []):
    for track in binding.get_tracks():
        for section in track.get_sections():
            for channel in section.get_all_channels():
                keys = channel.get_keys()
                if len(keys) <= frame:
                    continue
                value = keys[frame].get_value()
                for k in keys:
                    channel.remove_key(k)
                channel.add_key(unreal.FrameNumber(0), value, interpolation=unreal.MovieSceneKeyInterpolation.CONSTANT)
                held += 1
seq.set_playback_start(0)
seq.set_playback_end(frames)
for track in seq.get_tracks():
    for section in track.get_sections():
        section.set_range(0, frames)
unreal.EditorAssetLibrary.save_loaded_asset(seq)
unreal.log_warning("athenea: %s held at frame %d of %s, %d channels, %d frames" % (path, frame, source, held, frames))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
