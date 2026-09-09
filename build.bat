@echo off
rem build.bat - MSVC one-shot build (x64, /W4 /WX, /MT, C++20)
rem usage: build.bat        -> build full app
rem        build.bat spike  -> build spike only
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

if "%~1"=="spike" (
    cl /nologo /W4 /WX /utf-8 /std:c++20 /O2 /MT ^
       spike\spike.cpp ^
       /Fe:build\spike.exe /Fo:build\spike.obj ^
       /link user32.lib gdi32.lib /SUBSYSTEM:CONSOLE
    if errorlevel 1 exit /b 1
    echo [OK] build\spike.exe
    exit /b 0
)

if "%~1"=="min" (
    cl /nologo /W4 /WX /utf-8 /std:c++20 /O2 /MT ^
       spike\minmain.cpp ^
       /Fe:build\minmain.exe /Fo:build\minmain.obj ^
       /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup
    if errorlevel 1 exit /b 1
    echo [OK] build\minmain.exe
    exit /b 0
)

if "%~1"=="dump" (
    link /dump /imports build\PrivacyScreen.exe | findstr /i ".dll"
    exit /b 0
)

if "%~1"=="minlogger" (
    cl /nologo /W4 /WX /utf-8 /std:c++20 /O2 /MT ^
       spike\minlogger.cpp ^
       /Fe:build\minlogger.exe /Fo:build\minlogger.obj ^
       /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup
    if errorlevel 1 exit /b 1
    echo [OK] build\minlogger.exe
    exit /b 0
)

if "%~1"=="mincs" (
    cl /nologo /W4 /WX /utf-8 /std:c++20 /O2 /MT /MAP:build\mincs.map ^
       spike\mincs.cpp ^
       /Fe:build\mincs.exe /Fo:build\mincs.obj ^
       /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup
    if errorlevel 1 exit /b 1
    echo [OK] build\mincs.exe
    exit /b 0
)

set "SOURCES="
for %%f in (src\*.cpp src\ipc\*.cpp src\monitor\*.cpp src\overlay\*.cpp src\privacy\*.cpp src\util\*.cpp src\images\*.cpp) do set "SOURCES=!SOURCES! %%f"
if not defined SOURCES (
    echo [ERROR] no sources found
    exit /b 1
)

cl /nologo /W4 /WX /utf-8 /std:c++20 /O2 /MT ^
   /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 ^
   %SOURCES% ^
   /Fe:build\PrivacyScreen.exe /Fo:build\ ^
   /link user32.lib gdi32.lib shell32.lib advapi32.lib ntdll.lib gdiplus.lib ole32.lib ^
   /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /MAP:build\PrivacyScreen.map
if errorlevel 1 exit /b 1
echo [OK] build\PrivacyScreen.exe
endlocal
