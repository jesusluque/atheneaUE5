#!/bin/bash
# Copyright (c) 2026 jesus luque.
#
# Times a level of the test project in -game mode on a Windows machine reached
# by ssh: the CSV profiler, a 30 fps cap, 450 frames; then prints the medians
# of the frame and of the splat stages over the last 300.
#
#   ue_time.sh /Game/Level out.csv ["cvar value,cvar value"]
#
# UE_HOST     the ssh host of the Windows machine (default: windows)
# UE_ROOT     UE 5.8's folder there (default: C:\Program Files\Epic Games\UE_5.8)
# UE_PROJECT  the .uproject there (default: %USERPROFILE%\athenea-unreal\TestProj\TestProj.uproject)
set -e
LEVEL=${1:-/Game/Test}; OUT=${2:-ue_time.csv}; EXTRA=${3:+,$3}
HOST=${UE_HOST:-windows}
ROOT=${UE_ROOT:-'C:\Program Files\Epic Games\UE_5.8'}
PROJECT=${UE_PROJECT:-'%USERPROFILE%\athenea-unreal\TestProj\TestProj.uproject'}
CSVDIR='%LOCALAPPDATA%\UnrealEngine\5.8\Saved\Profiling\CSV'

ssh $HOST 'nvidia-smi --query-gpu=utilization.gpu,clocks.sm,temperature.gpu --format=csv,noheader' | tr -d '\r' | sed 's/^/before: /'
ssh $HOST "if exist \"$CSVDIR\" rmdir /s /q \"$CSVDIR\""
ssh $HOST "\"$ROOT\\Engine\\Binaries\\Win64\\UnrealEditor.exe\" \"$PROJECT\" $LEVEL -game -RenderOffscreen -ResX=1280 -ResY=720 -windowed -unattended -nosplash -nosound -csvGpuStats -csvCaptureFrames=450 -ExecCmds=\"r.GPUStatsEnabled 1,t.MaxFPS 30,r.VSync 0$EXTRA\" > NUL 2>&1" &

# The profiler opens the file when the capture starts and writes it at the end
# (and -csvExitOnCompletion does not end the process in 5.8.4): wait for bytes,
# then for them to stop growing.
last=0; f=""
for i in $(seq 1 120); do
  sleep 5
  f=$(ssh $HOST "dir /b \"$CSVDIR\\*.csv\"" 2>/dev/null | tr -d '\r' | head -1)
  [ -z "$f" ] && continue
  size=$(ssh $HOST "powershell -NoProfile -Command \"(Get-Item \$env:LOCALAPPDATA\\UnrealEngine\\5.8\\Saved\\Profiling\\CSV\\*.csv).Length\"" 2>/dev/null | tr -d '\r ')
  [ "${size:-0}" -gt 0 ] && [ "$size" = "$last" ] && break
  last=${size:-0}
done
ssh $HOST 'nvidia-smi --query-gpu=clocks.sm,temperature.gpu --format=csv,noheader' | tr -d '\r' | sed 's/^/end: /'
ssh $HOST 'taskkill /im UnrealEditor.exe /f' >/dev/null 2>&1 || true
wait || true

scp -q "$HOST:AppData/Local/UnrealEngine/5.8/Saved/Profiling/CSV/$f" "$OUT"
python3 - "$OUT" <<'PY'
import csv, statistics, sys
csv.field_size_limit(sys.maxsize)
rows = list(csv.reader(open(sys.argv[1])))
h = rows[0]; data = rows[1:-1]
print(len(data), "frames; medians over the last 300")
for c in [x for x in h if x in ("FrameTime", "GPUTime") or x.startswith("GPU/Gaussians") or x == "GPU/Unaccounted"]:
    i = h.index(c); v = []
    for r in data[-300:]:
        try: v.append(float(r[i]))
        except (ValueError, IndexError): pass
    if v:
        print(f"{c:28s} median {statistics.median(v):8.3f} ms  min {min(v):8.3f}")
PY
