# Copyright (c) 2026 jesus luque.
# Stops the -game runs of a project (ue_time.sh's), and nothing else: an
# editor open on the machine is left alone.  stop_game.ps1 PROJECT [-DryRun]
param([string]$Project, [switch]$DryRun)
Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
  Where-Object { $_.CommandLine -like "*$Project.uproject*" -and $_.CommandLine -like "* -game *" } |
  ForEach-Object { if ($DryRun) { "would stop $($_.ProcessId)" } else { Stop-Process -Id $_.ProcessId -Force; "stopped $($_.ProcessId)" } }
