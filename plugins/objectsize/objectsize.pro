#-------------------------------------------------
# LibreCAD object size plugin
#-------------------------------------------------

QT       += gui widgets
TEMPLATE = lib
CONFIG  += plugin
VERSION  = 1.0.0
TARGET   = $$qtLibraryTarget(objectsize)

GENERATED_DIR = ../../generated/plugin/objectsize
include(../../common.pri)
# This plugin does not use SVG and the bundled Qt SDK may omit QtSvg.
QT -= svg

INCLUDEPATH += ../../librecad/src/plugins

SOURCES += objectsize.cpp
HEADERS += objectsize.h

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
