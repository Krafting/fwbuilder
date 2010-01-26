include(../../../../qmake.inc)
QT += gui network
TEMPLATE = app
LANGUAGE = C++
QMAKE_CXX = g++
CONFIG += console

INCLUDEPATH += ../../.. ../.. ../../.ui ../../../compiler_lib/

HEADERS += ImporterTest.h \
    	../../IPTImporter.h \
        ../../config.h \
        ../../utils_no_qt.h \
        ../../FWBTree.h \
        ../../Importer.h \
        ../../IOSImporter.h \
        ../../IPTImporter.h \
        ../../../parsers/IPTCfgLexer.hpp \
        ../../../parsers/IPTCfgParser.hpp \
        ../../../parsers/IOSCfgLexer.hpp \
        ../../../parsers/IOSCfgParser.hpp \
        ../../../compiler_lib/interfaceProperties.h \
        ../../../compiler_lib/interfacePropertiesObjectFactory.h \
        ../../../compiler_lib/linux24Interfaces.h \
        ../../../compiler_lib/bsdInterfaces.h \
        ../../../compiler_lib/iosInterfaces.h \
        ../../../compiler_lib/pixInterfaces.h

SOURCES += main.cpp \
        ImporterTest.cpp \
    	../../utils_no_qt.cpp \
    	../../FWBTree.cpp \
        ../../Importer.cpp \
        ../../IOSImporter.cpp \
        ../../IOSImporterRun.cpp \
        ../../IPTImporter.cpp \
        ../../IPTImporterRun.cpp \
        ../../../parsers/IPTCfgLexer.cpp \
        ../../../parsers/IPTCfgParser.cpp \
        ../../../parsers/IOSCfgLexer.cpp \
        ../../../parsers/IOSCfgParser.cpp \
        ../../../compiler_lib/interfaceProperties.cpp \
        ../../../compiler_lib/interfacePropertiesObjectFactory.cpp \
        ../../../compiler_lib/linux24Interfaces.cpp \
        ../../../compiler_lib/bsdInterfaces.cpp \
        ../../../compiler_lib/iosInterfaces.cpp \
        ../../../compiler_lib/pixInterfaces.cpp


TARGET = ImporterTest
CONFIG -= release
CONFIG += debug
OBJECTS_DIR = .
QMAKE_CXXFLAGS += -g -fprofile-arcs -ftest-coverage -O0 $$CPPUNIT_CFLAGS
QMAKE_CLEAN = *.gc??
LIBS += $$LIBS_FWCOMPILER $$LIBS_FWBUILDER $$CPPUNIT_LIBS
LIBS += -lgcov -lcppunit
DEPENDPATH = ../../../common

!win32:LIBS += ../../../common/libcommon.a
!win32:PRE_TARGETDEPS = ../../../common/libcommon.a

win32:CONFIG += console

win32:LIBS += ../../../common/release/common.lib
win32:PRE_TARGETDEPS = ../../../common/release/common.lib

INCLUDEPATH	 += $$ANTLR_INCLUDEPATH
LIBS		 += ../../../parsers/libfwbparser.a  $$ANTLR_LIBS
DEFINES		 += $$ANTLR_DEFINES


run.commands = echo "Running tests..." && \
    ./${TARGET} && echo "Running gcov..." && \
    gcov ${SOURCES} >/dev/null 2>/dev/null && \
    echo "OK" || { echo "FAILED"; exit 1; }

run.depends = all
QMAKE_EXTRA_TARGETS += run
