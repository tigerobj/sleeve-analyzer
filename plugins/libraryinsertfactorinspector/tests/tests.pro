QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console testcase c++11
TARGET = FactorInspectorSnapshotTests

INCLUDEPATH += .. ../../../librecad/src/plugins

SOURCES += \
    FactorInspectorSnapshotTests.cpp \
    ../factorinspectorsnapshot.cpp

HEADERS += ../factorinspectorsnapshot.h
