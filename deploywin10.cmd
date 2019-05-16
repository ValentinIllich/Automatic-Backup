REM debug versions

xcopy /D /Y %1\bin\Qt5Cored.dll %2\debug
xcopy /D /Y %1\bin\Qt5Widgetsd.dll %2\debug
xcopy /D /Y %1\bin\Qt5Guid.dll %2\debug

xcopy /D /Y %1\bin\libgcc_s_dw2-1.dll %2\debug
xcopy /D /Y %1\bin\libwinpthread-1.dll %2\debug
xcopy /D /Y %1\bin\libstdc++-6.dll %2\debug

REM relese versions

xcopy /D /Y %1\bin\Qt5Core.dll %2\release
xcopy /D /Y %1\bin\Qt5Widgets.dll %2\release
xcopy /D /Y %1\bin\Qt5Gui.dll %2\release

xcopy /D /Y %1\bin\libgcc_s_dw2-1.dll %2\release
xcopy /D /Y %1\bin\libwinpthread-1.dll %2\release
xcopy /D /Y %1\bin\libstdc++-6.dll %2\release
