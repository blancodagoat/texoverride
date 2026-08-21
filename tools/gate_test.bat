@echo off
REM Lift the safety gate out of dllmain.cpp and run gate_test.cpp against it, so the test can
REM never drift from the shipping code. Run this after touching isAllowedKey.
cd /d "%~dp0"
powershell -NoProfile -Command "$l=Get-Content ..\dllmain.cpp; $a=($l | Select-String -Pattern '^static bool isPedCollection' | Select-Object -First 1).LineNumber-1; $b=($l | Select-String -Pattern '^static bool isAllowedKey' | Select-Object -First 1).LineNumber-1; while ($l[$b] -ne '}') { $b++ }; $l[$a..$b] | Set-Content gate_extracted.inc" || exit /b 1
findstr /c:"isOverrideExt" gate_extracted.inc >nul || (echo extraction missed isOverrideExt & exit /b 1)

if "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cl /nologo /std:c++17 /EHsc /O1 gate_test.cpp /Fe:gate_test.exe >nul || exit /b 1
.\gate_test.exe
set RC=%ERRORLEVEL%
del /q *.obj gate_test.exe gate_extracted.inc 2>nul
exit /b %RC%
