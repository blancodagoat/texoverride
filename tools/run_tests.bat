@echo off
REM Builds and runs every test in this folder. Exits nonzero if any of them fails, so CI
REM can use it as a gate. Run it after touching the gate, the settings file, the sweep that
REM revalidates slot names, ctlPath, the parsers (update, RSC, placement xml), the path layer, or
REM the vendored minhook.
cd /d "%~dp0.."

if "%VSCMD_ARG_TGT_ARCH%"=="x64" goto run
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo Visual Studio Build Tools not found. && exit /b 1
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" echo Found Visual Studio but not the C++ tools. && exit /b 1
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:run
set "OUT=%TEMP%\texoverride-tests"
if not exist "%OUT%" mkdir "%OUT%"
REM One source list for all of them: a test links what it needs and ignores the rest.
set "SRC=src\core\utils.cpp src\core\state.cpp src\core\logger.cpp src\core\settings.cpp src\core\pattern.cpp src\streaming\gate.cpp src\streaming\rsc.cpp src\features\placement.cpp src\features\update.cpp"
set "FAILED="

call :one test_gate_keys || set "FAILED=%FAILED% test_gate_keys"
call :one test_slotkey   || set "FAILED=%FAILED% test_slotkey"
call :one test_settings  || set "FAILED=%FAILED% test_settings"
call :one test_reval     || set "FAILED=%FAILED% test_reval"
call :one test_ctlpath   || set "FAILED=%FAILED% test_ctlpath"
call :one test_parsers   || set "FAILED=%FAILED% test_parsers"
call :onemh test_minhook || set "FAILED=%FAILED% test_minhook"

del /q "%OUT%\*.obj" 2>nul
if not "%FAILED%"=="" echo. && echo FAILED:%FAILED% && exit /b 1
echo.
echo all tests passed
exit /b 0

:one
cl /nologo /std:c++17 /W4 /EHsc /MT /I src tools\%1.cpp %SRC% /Fe:"%OUT%\%1.exe" /Fo:"%OUT%\\" >nul || exit /b 1
"%OUT%\%1.exe" || exit /b 1
exit /b 0

:onemh
REM Needs the vendored minhook instead of the shared source list. Same /W0 split as
REM build.bat: minhook is third-party C and warns about its own style at /W4.
cl /nologo /c /W0 /MT /I minhook\include minhook\src\buffer.c minhook\src\hook.c minhook\src\trampoline.c minhook\src\hde\hde64.c /Fo:"%OUT%\\" >nul || exit /b 1
cl /nologo /std:c++17 /W4 /EHsc /MT /I minhook\include tools\%1.cpp "%OUT%\buffer.obj" "%OUT%\hook.obj" "%OUT%\trampoline.obj" "%OUT%\hde64.obj" /Fe:"%OUT%\%1.exe" /Fo:"%OUT%\\" >nul || exit /b 1
"%OUT%\%1.exe" || exit /b 1
exit /b 0
