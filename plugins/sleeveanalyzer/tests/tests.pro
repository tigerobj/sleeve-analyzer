QT += core gui
QT -= widgets
TEMPLATE = app
CONFIG += console testcase c++11
TARGET = SleeveAnalyzerTests

INCLUDEPATH += .. ../../../librecad/src/plugins

SOURCES += \
    SleeveAnalyzerTests.cpp \
    ../LongSleeveGenerator.cpp \
    ../SleeveAnalyzer.cpp \
    ../SleeveGeometry.cpp \
    ../SleevePatternFeatures.cpp \
    ../SleeveSideGeometry.cpp \
    ../SleeveTargetGeometry.cpp \
    ../SleeveSizeTable.cpp

HEADERS += \
    ../LongSleeveGenerator.h \
    ../SleeveAnalyzer.h \
    ../SleeveGeometry.h \
    ../SleevePatternFeatures.h \
    ../SleeveSideGeometry.h \
    ../SleeveTargetGeometry.h \
    ../SleeveSizeTable.h
