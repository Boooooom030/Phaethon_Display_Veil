@echo off
rem build.bat - MSVC one-shot build (x64, /W4 /WX, /MT, C++20)
rem usage: build.bat -> build\Phaethon.exe
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo [ERROR] Visual Studio with C++ tools not found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 (
    echo [ERROR] vcvarsall failed.
    exit /b 1
)

if not exist build mkdir build

rem compile resources (icon) when the rc file exists
set "RES_OBJ="
if exist src\app.rc (
    rc /nologo /fo build\app.res src\app.rc
    if errorlevel 1 exit /b 1
    set "RES_OBJ=build\app.res"
)

set "SOURCES="
for %%f in (src\*.cpp src\ipc\*.cpp src\monitor\*.cpp src\overlay\*.cpp src\privacy\*.cpp src\util\*.cpp src\images\*.cpp) do set "SOURCES=!SOURCES! %%f"
if not defined SOURCES (
    echo [ERROR] no sources found
    exit /b 1
)

cl /nologo /W4 /WX /utf-8 /std:c++20 /O2 /MT ^
   /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 ^
   %SOURCES% %RES_OBJ% ^
   /Fe:build\Phaethon.exe /Fo:build\ ^
   /link user32.lib gdi32.lib shell32.lib advapi32.lib ntdll.lib gdiplus.lib ole32.lib ^
   /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup
if errorlevel 1 exit /b 1
echo [OK] build\Phaethon.exe
endlocal
