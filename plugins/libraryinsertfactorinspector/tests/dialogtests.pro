QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console testcase c++11
TARGET = FactorInspectorDialogTests

INCLUDEPATH += .. ../../../librecad/src/plugins

SOURCES += \
    FactorInspectorDialogTests.cpp \
    ../factorinspectordialog.cpp \
    ../factorinspectorsnapshot.cpp

HEADERS += \
    ../factorinspectorsnapshot.h \
    ../factorinspectordialog.h
