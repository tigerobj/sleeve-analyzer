QT += gui widgets
TEMPLATE = lib
CONFIG += plugin
VERSION = 1.0.0
TARGET = $$qtLibraryTarget(sleeveanalyzer)

GENERATED_DIR = ../../generated/plugin/sleeveanalyzer
include(../../common.pri)

INCLUDEPATH += ../../librecad/src/plugins

SOURCES += \
    LongSleeveGenerator.cpp \
    SleeveAnalyzer.cpp \
    SleeveDebugDialog.cpp \
    SleeveGeometry.cpp \
    SleevePatternFeatures.cpp \
    SleeveSideGeometry.cpp \
    SleeveTargetGeometry.cpp \
    SleeveSizeTable.cpp \
    sleeveanalyzerplugin.cpp

HEADERS += \
    LongSleeveGenerator.h \
    SleeveAnalyzer.h \
    SleeveDebugDialog.h \
    SleeveGeometry.h \
    SleevePatternFeatures.h \
    SleeveSideGeometry.h \
    SleeveTargetGeometry.h \
    SleeveSizeTable.h \
    sleeveanalyzerplugin.h

DISTFILES += sleeveanalyzer.json sleeve_sizes.json

copy_sleeve_sizes.target = copy_sleeve_sizes
copy_sleeve_sizes.commands = $$QMAKE_COPY $$shell_path($$PWD/sleeve_sizes.json) $$shell_path($$DESTDIR/sleeve_sizes.json)
QMAKE_EXTRA_TARGETS += copy_sleeve_sizes
POST_TARGETDEPS += copy_sleeve_sizes

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
