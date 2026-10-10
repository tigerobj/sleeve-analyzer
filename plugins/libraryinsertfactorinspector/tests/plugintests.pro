QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console testcase c++11
TARGET = LibraryInsertFactorInspectorPluginTests

INCLUDEPATH += .. ../../../librecad/src/plugins

SOURCES += \
    PluginContractTests.cpp \
    ../factorinspectordialog.cpp \
    ../factorinspectorsnapshot.cpp \
    ../libraryinsertfactorinspector.cpp

HEADERS += \
    ../factorinspectordialog.h \
    ../factorinspectorsnapshot.h \
    ../libraryinsertfactorinspector.h

DISTFILES += ../libraryinsertfactorinspector.json
