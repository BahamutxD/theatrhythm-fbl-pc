@echo off
rem Starts the game with the glitch diagnostics armed (idle until the other .bat files are used).
rem The game log goes to diagnose\theatrhythm.log.
set "HERE=%~dp0"
del "%HERE%capture.trigger" "%HERE%reload.trigger" 2>nul
> "%HERE%toggles.txt" echo 0
set "THEATRHYTHM_TOGGLE_FILE=%HERE%toggles.txt"
set "THEATRHYTHM_CAPTURE_TRIGGER=%HERE%capture.trigger"
set "THEATRHYTHM_CAPTURE_DIR=%HERE%captures"
set "THEATRHYTHM_RELOAD_TRIGGER=%HERE%reload.trigger"
rem Every routine event in the log too, not only faults and errors.
set "THEATRHYTHM_VERBOSE=1"
start "" /b "%HERE%..\theatrhythm-fbl.exe" %* > "%HERE%theatrhythm.log" 2>&1
