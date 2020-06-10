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
    cleanupdialog.h \
    backupSplash.h \
    utilities.h \
    backupengine.h \
    backupdirstruct.h
FORMS += backupwindow.ui \
    mainwindow.ui \
    cleanupdialog.ui
SOURCES += backupExecuter.cpp \
    backupMain.cpp \
    main.cpp \
    cleanupdialog.cpp \
    backupSplash.cpp \
    utilities.cpp \
    backupengine.cpp \
    authexec.c \
    backupdirstruct.cpp
RESOURCES += backupwindow.qrc
win32 { 
    MOC_DIR = c:/tmp/backup_obj
    UI_DIR = c:/tmp/backup_obj
    OBJECTS_DIR = c:/tmp/backup_obj
    RCC_DIR = c:/tmp/backup_obj
    RC_FILE = ressources/backup.rc
}
macx { 
#    QMAKE_MAKEFILE = MacMakefile
    MOC_DIR = /private/var/tmp/backup_obj
    UI_DIR = /private/var/tmp/backup_obj
    OBJECTS_DIR = /private/var/tmp/backup_obj
    RCC_DIR = /private/var/tmp/backup_obj
    ICON = ressources/backup.icns
    QMAKE_INFO_PLIST = ressources/Info_mac.plist
    LIBS += -framework Security
}
OTHER_FILES += distribute/welcome.html \
    ressources/help.html \
    ReleaseNotes.txt \
    ressources/backup.rc \
    ressources/Info_mac.plist

DISTFILES += \
    Issues-todo.txt \
    ressources/version.inc
