@echo off
setlocal

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"
set "QT_DIR=%PROJ_DIR%\qt5\5.15.0\msvc2019_64"
set "BUILD_DIR=%PROJ_DIR%\build"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

cl.exe /nologo /O2 /MD /EHsc /Zc:__cplusplus /std:c++17 /utf-8 ^
    /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING ^
    /DUNICODE /D_UNICODE /DWIN32 /DWIN64 /DNDEBUG ^
    /DQT_NO_DEBUG /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%PROJ_DIR%\src" ^
    "%PROJ_DIR%\test_plugin.cpp" ^
    /Fo"%BUILD_DIR%\\" ^
    /link /NOLOGO ^
    /LIBPATH:"%QT_DIR%\lib" ^
    Qt5Widgets.lib Qt5Gui.lib Qt5Core.lib ole32.lib user32.lib shell32.lib ^
    /OUT:"%BUILD_DIR%\test_plugin.exe"
if errorlevel 1 exit /b 1

set "PATH=C:\Program Files\OpenRGB;%PATH%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=C:\Program Files\OpenRGB\platforms"
"%BUILD_DIR%\test_plugin.exe"
