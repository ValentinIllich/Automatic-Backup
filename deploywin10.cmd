REM debug versions

xcopy /D /Y %1\bin\Qt5Cored.dll %3\debug
xcopy /D /Y %1\bin\Qt5Widgetsd.dll %3\debug
xcopy /D /Y %1\bin\Qt5Guid.dll %3\debug

xcopy /D /Y %2\bin\libgcc_s_seh-1.dll %3\debug
xcopy /D /Y %2\bin\libwinpthread-1.dll %3\debug
xcopy /D /Y %2\bin\libstdc++-6.dll %3\debug

REM relese versions

xcopy /D /Y %1\bin\Qt5Core.dll %3\release
xcopy /D /Y %1\bin\Qt5Widgets.dll %3\release
xcopy /D /Y %1\bin\Qt5Gui.dll %3\release

xcopy /D /Y %2\bin\libgcc_s_seh-1.dll %3\release
xcopy /D /Y %2\bin\libwinpthread-1.dll %3\release
xcopy /D /Y %2\bin\libstdc++-6.dll %3\release
