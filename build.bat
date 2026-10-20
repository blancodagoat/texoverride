@echo off
REM Build texoverride.asi. Needs Visual Studio Build Tools with the "Desktop development with
REM C++" workload. Run from a "x64 Native Tools Command Prompt", or let this find vcvars.
REM
REM   build.bat        the release build, texoverride.asi
REM   build.bat dev    a DEV build, texoverride-dev.asi, with the LOG_DEV tracing compiled in.
REM                    Never release it. The name differs on purpose so it cannot be mistaken
REM                    for the release one.

set "DEFS="
set "OUT=texoverride.asi"
if /i "%~1"=="dev" (
  set "DEFS=/DTEXOVERRIDE_DEV"
  set "OUT=texoverride-dev.asi"
)

if "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Build Tools not found. Install "Desktop development with C++" from:
  echo   https://visualstudio.microsoft.com/downloads/  ^(Build Tools for Visual Studio^)
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -property installationPath`) do set "VSPATH=%%i"
)
if "%VSPATH%"=="" (
  echo Found Visual Studio but not the C++ tools. Add the "Desktop development with C++" workload.
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
rc /nologo /fo texoverride.res texoverride.rc || exit /b 1
REM /MT so it does not need the VC runtime DLLs in FiveM's directory.
REM /EHsc for std::string/std::vector. No /clr — a managed DLL is refused outright by asi-five.
REM minhook is vendored C and warns at /W4 about its own style. Compiled on its own so the
REM warnings the build prints are only ever ours.
cl /nologo /c /W0 /O2 /MT /DNDEBUG /I minhook\include ^
   minhook\src\buffer.c minhook\src\hook.c minhook\src\trampoline.c minhook\src\hde\hde64.c || exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DNDEBUG %DEFS% /LD /I minhook\include /I src ^
   dllmain.cpp src\core\*.cpp src\streaming\*.cpp src\features\*.cpp ^
   buffer.obj hook.obj trampoline.obj hde64.obj ^
   texoverride.res /Fe:%OUT% /link /DLL user32.lib /Brepro || exit /b 1

del /q *.obj *.res *.exp *.lib 2>nul
echo.
if not "%DEFS%"=="" goto devdone
echo Built texoverride.asi
echo Copy it to: %LOCALAPPDATA%\FiveM\FiveM.app\plugins\
exit /b 0
:devdone
echo Built texoverride-dev.asi  ** DEV BUILD: extra DEV log lines, do not release **
echo Install it AS texoverride.asi in FiveM.app\plugins (move the release one aside first).
echo Two copies of the plugin in that folder would both load and both hook.
