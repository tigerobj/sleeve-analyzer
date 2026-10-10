QT += gui widgets
TEMPLATE = lib
CONFIG += plugin
VERSION = 1.0.0
TARGET = $$qtLibraryTarget(LibraryInsertFactorInspector)

GENERATED_DIR = ../../generated/plugin/libraryinsertfactorinspector
include(../../common.pri)

INCLUDEPATH += ../../librecad/src/plugins

SOURCES += \
    factorinspectordialog.cpp \
    factorinspectorsnapshot.cpp \
    libraryinsertfactorinspector.cpp

HEADERS += \
    factorinspectordialog.h \
    factorinspectorsnapshot.h \
    libraryinsertfactorinspector.h

win32:LIBS += -ladvapi32

DISTFILES += libraryinsertfactorinspector.json

win32 {
    DESTDIR = ../../windows/resources/plugins
}
unix {
    macx {
        DESTDIR = ../../LibreCAD.app/Contents/Resources/plugins
    } else {
        DESTDIR = ../../unix/resources/plugins
    }
}
