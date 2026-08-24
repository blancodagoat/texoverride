@echo off
REM Compile the shipping manifest parser into a small Windows fixture and exercise its fail-closed cases.
cd /d "%~dp0"

if "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cl /nologo /std:c++17 /EHsc /O1 /MT /I ..\minhook\include ^
   pack_manifest_test.cpp ..\minhook\src\buffer.c ..\minhook\src\hook.c ^
   ..\minhook\src\trampoline.c ..\minhook\src\hde\hde64.c ^
   /Fe:pack_manifest_test.exe /link user32.lib || exit /b 1
.\pack_manifest_test.exe
set RC=%ERRORLEVEL%
del /q *.obj pack_manifest_test.exe 2>nul
exit /b %RC%
