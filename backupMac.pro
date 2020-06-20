# #####################################################################
# Automatically generated by qmake (2.01a) Mo Jan 1 19:37:32 2007
# #####################################################################

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TEMPLATE = app
TARGET = backup

# Input
HEADERS += backupExecuter.h \
    backupConfigData.h \
    backupMain.h \
    cleanupDialog.h \
    backupSplash.h \
    filterSettings.h \
    utilities.h \
    backupEngine.h \
    backupDirStruct.h
FORMS += backupwindow.ui \
    filterSettings.ui \
    mainwindow.ui \
    cleanupDialog.ui
SOURCES += backupExecuter.cpp \
    backupMain.cpp \
    filterSettings.cpp \
    main.cpp \
    cleanupDialog.cpp \
    backupSplash.cpp \
    utilities.cpp \
    backupEngine.cpp \
    authexec.c \
    backupDirStruct.cpp
RESOURCES += backupwindow.qrc

CONFIG(debug, debug|release): BUILD = debug
CONFIG(release, debug|release): BUILD = release

win32 {
    OBJECTS_DIR = c:/tmp/backup_$${BUILD}_obj
    UI_DIR = c:/tmp/backup_$${BUILD}_obj
    MOC_DIR = c:/tmp/backup_$${BUILD}_obj
    RCC_DIR = c:/tmp/backup_$${BUILD}_obj
    RC_FILE = ressources/backup.rc
}
macx {
    OBJECTS_DIR = /private/var/tmp/backup_$${BUILD}_obj
    UI_DIR = /private/var/tmp/backup_$${BUILD}_obj
    MOC_DIR = /private/var/tmp/backup_$${BUILD}_obj
    RCC_DIR = /private/var/tmp/backup_$${BUILD}_obj
    ICON = ressources/backup.icns
    QMAKE_INFO_PLIST = ressources/Info_mac.plist
    LIBS += -framework Security
}
linux {
    OBJECTS_DIR = /var/tmp/backup_$${BUILD}_obj
    UI_DIR = /var/tmp/backup_$${BUILD}_obj
    MOC_DIR = /var/tmp/backup_$${BUILD}_obj
    RCC_DIR = /var/tmp/backup_$${BUILD}_obj
    ICON = ressources/backup.icns
    QMAKE_INFO_PLIST = ressources/Info_mac.plist
}

OTHER_FILES += distribute/welcome.html \
    ressources/help.html \
    ReleaseNotes.txt \
    ressources/backup.rc \
    ressources/Info_mac.plist

DISTFILES += \
    Issues-todo.txt \
    ressources/version.inc
