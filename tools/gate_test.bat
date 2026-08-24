@echo off
REM Run gate_test.cpp against src/gate.h directly.
REM Run this after touching isAllowedKey or any safety gate rules.
cd /d "%~dp0"

if "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -property installationPath`) do set "VSPATH=%%i"
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cl /nologo /std:c++17 /EHsc /O1 /I..\src /I..\minhook\include gate_test.cpp ..\src\streaming\gate.cpp ..\src\core\utils.cpp ..\src\core\logger.cpp ..\src\core\state.cpp /Fe:gate_test.exe >nul || exit /b 1
.\gate_test.exe
set RC=%ERRORLEVEL%
del /q *.obj gate_test.exe 2>nul
exit /b %RC%
