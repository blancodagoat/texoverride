@echo off
REM DEV build: same as build.bat plus TEXOVERRIDE_DEV, which turns on the LOG_DEV tracing.
REM Never release the .asi this produces - use build.bat for that.
REM Build texoverride.asi. Needs Visual Studio Build Tools with the "Desktop development with
REM C++" workload. Run from a "x64 Native Tools Command Prompt", or let this find vcvars.

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
cl /nologo /std:c++17 /O2 /MT /EHsc /DNDEBUG /DTEXOVERRIDE_DEV /LD /I minhook\include /I src ^
   dllmain.cpp src\core\*.cpp src\streaming\*.cpp src\features\*.cpp ^
   minhook\src\buffer.c minhook\src\hook.c minhook\src\trampoline.c minhook\src\hde\hde64.c ^
   texoverride.res /Fe:texoverride.asi /link /DLL user32.lib /Brepro || exit /b 1

del /q *.obj *.res *.exp *.lib 2>nul
echo.
echo Built texoverride.asi  ** DEV BUILD: extra DEV log lines, do not release **
echo Copy it to: %LOCALAPPDATA%\FiveM\FiveM.app\plugins\
