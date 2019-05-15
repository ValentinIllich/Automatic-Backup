REM debug versions

xcopy /D /Y C:\Qt\5.12.2\mingw73_32\bin\Qt5Cored.dll %1\debug
xcopy /D /Y C:\Qt\5.12.2\mingw73_32\bin\Qt5Widgetsd.dll %1\debug
xcopy /D /Y C:\Qt\5.12.2\mingw73_32\bin\Qt5Guid.dll %1\debug

xcopy /D /Y C:\Qt\Tools\mingw730_32\bin\libgcc_s_dw2-1.dll %1\debug
xcopy /D /Y C:\Qt\Tools\mingw730_32\bin\libwinpthread-1.dll %1\debug
xcopy /D /Y C:\Qt\Tools\mingw730_32\bin\libstdc++-6.dll %1\debug

REM relese versions

xcopy /D /Y C:\Qt\5.12.2\mingw73_32\bin\Qt5Core.dll %1\release
xcopy /D /Y C:\Qt\5.12.2\mingw73_32\bin\Qt5Widgets.dll %1\release
xcopy /D /Y C:\Qt\5.12.2\mingw73_32\bin\Qt5Gui.dll %1\release

xcopy /D /Y C:\Qt\Tools\mingw730_32\bin\libgcc_s_dw2-1.dll %1\release
xcopy /D /Y C:\Qt\Tools\mingw730_32\bin\libwinpthread-1.dll %1\release
xcopy /D /Y C:\Qt\Tools\mingw730_32\bin\libstdc++-6.dll %1\release
