@echo off
setlocal

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"

if defined QT_ROOT_DIR set "QT_DIR=%QT_ROOT_DIR%"
if not defined QT_DIR set "QT_DIR=%PROJ_DIR%\qt5\5.15.0\msvc2019_64"

set "BUILD_DIR=%PROJ_DIR%\build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if not defined PLUGIN_VERSION set "PLUGIN_VERSION=1.0.0"
if defined PLUGIN_COMMIT goto :have_commit
pushd "%PROJ_DIR%"
for /f "delims=" %%i in ('git rev-parse --short^=7 HEAD 2^>nul') do set "PLUGIN_COMMIT=%%i"
popd
if not defined PLUGIN_COMMIT set "PLUGIN_COMMIT=local"
:have_commit

where cl.exe >nul 2>nul
if not errorlevel 1 goto :have_msvc

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :fallback_msvc

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -products * -latest -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL_DIR=%%i"
if not defined VS_INSTALL_DIR goto :fallback_msvc
if not exist "%VS_INSTALL_DIR%\VC\Auxiliary\Build\vcvars64.bat" goto :fallback_msvc

call "%VS_INSTALL_DIR%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
goto :have_msvc

:fallback_msvc
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

:have_msvc
echo [1/2] Running Qt 5.15.0 moc...
"%QT_DIR%\bin\moc.exe" ^
    -DUNICODE -D_UNICODE -DWIN32 -D_ENABLE_EXTENDED_ALIGNED_STORAGE -DWIN64 -DNDEBUG ^
    -DQT_NO_DEBUG -DQT_PLUGIN -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB ^
    -I"%QT_DIR%\include" ^
    -I"%QT_DIR%\include\QtWidgets" ^
    -I"%QT_DIR%\include\QtGui" ^
    -I"%QT_DIR%\include\QtCore" ^
    -I"%PROJ_DIR%\src" ^
    "%PROJ_DIR%\src\OpenRGBAudioRefreshPlugin.h" -o "%BUILD_DIR%\moc_OpenRGBAudioRefreshPlugin.cpp"
if errorlevel 1 exit /b 1

echo [2/2] Compiling and linking OpenRGBAudioRefreshPlugin.dll (v%PLUGIN_VERSION% @ %PLUGIN_COMMIT%)...
cl.exe /nologo /O2 /MD /EHsc /Zc:__cplusplus /std:c++17 /utf-8 ^
    /FI"%PROJ_DIR%\src\Qt5MsvcCompat.h" ^
    /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING ^
    /DPLUGIN_VERSION_STR=\"%PLUGIN_VERSION%\" ^
    /DPLUGIN_COMMIT_STR=\"%PLUGIN_COMMIT%\" ^
    /DUNICODE /D_UNICODE /DWIN32 /DWIN64 /DNDEBUG ^
    /DQT_NO_DEBUG /DQT_PLUGIN /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%PROJ_DIR%\src" ^
    "%PROJ_DIR%\src\OpenRGBAudioRefreshPlugin.cpp" ^
    "%BUILD_DIR%\moc_OpenRGBAudioRefreshPlugin.cpp" ^
    /Fo"%BUILD_DIR%\\" ^
    /LD ^
    /link /NOLOGO ^
    /LIBPATH:"%QT_DIR%\lib" ^
    Qt5Widgets.lib Qt5Gui.lib Qt5Core.lib ole32.lib user32.lib advapi32.lib shell32.lib ^
    /IMPLIB:"%BUILD_DIR%\OpenRGBAudioRefreshPlugin.lib" ^
    /OUT:"%BUILD_DIR%\OpenRGBAudioRefreshPlugin.dll"
if errorlevel 1 exit /b 1

echo Build succeeded: %BUILD_DIR%\OpenRGBAudioRefreshPlugin.dll
