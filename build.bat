@echo off
rem Keep the source list below in sync with CMakeLists.txt. After moving the project,
rem delete build\*.obj first: they contain absolute paths from the old location.
setlocal EnableExtensions

set "ROOT=%~dp0"
set "OUT=%ROOT%build"

if not exist "%OUT%" mkdir "%OUT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] vswhere.exe not found. Install Visual Studio 2022 with the C++ workload.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [build] Visual Studio C++ toolchain not found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build] Failed to initialize the MSVC environment.
    exit /b 1
)

echo [build] Compiling resources...
rc /nologo /fo "%OUT%\starfield.res" "%ROOT%res\resource.rc"
if errorlevel 1 exit /b 1

rem advapi32 is required for the settings registry; comctl32 is not used because the
rem dialog is a plain single form.
echo [build] Compiling and linking Starfield.scr...
pushd "%OUT%"
cl /nologo /std:c++17 /utf-8 /O2 /GL /W4 /EHsc /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    "%ROOT%src\main.cpp" "%ROOT%src\renderer.cpp" "%ROOT%src\sky.cpp" "%ROOT%src\starfield.cpp" ^
    "%ROOT%src\settings.cpp" ^
    "%OUT%\starfield.res" ^
    /Fe:Starfield.scr ^
    /link /SUBSYSTEM:WINDOWS /LTCG user32.lib gdi32.lib winmm.lib shell32.lib advapi32.lib
if errorlevel 1 (
    popd
    exit /b 1
)
popd

echo [build] Done: %OUT%\Starfield.scr
endlocal
