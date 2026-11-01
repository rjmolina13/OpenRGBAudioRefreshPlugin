@echo off
setlocal

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"
set "QT_DIR=%PROJ_DIR%\qt5\5.15.0\msvc2019_64"
set "BUILD_DIR=%PROJ_DIR%\build"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

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

echo [2/2] Compiling and linking OpenRGBAudioRefreshPlugin.dll...
cl.exe /nologo /O2 /MD /EHsc /Zc:__cplusplus /std:c++17 /utf-8 ^
    /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING ^
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
    /OUT:"%BUILD_DIR%\OpenRGBAudioRefreshPlugin.dll"
if errorlevel 1 exit /b 1

echo Build succeeded: %BUILD_DIR%\OpenRGBAudioRefreshPlugin.dll
