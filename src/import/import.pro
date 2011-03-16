#-*- mode: makefile; tab-width: 4; -*-
#
include(../../qmake.inc)
#
TEMPLATE = lib
#

SOURCES = QStringListOperators.cpp \
	  objectMaker.cpp \
          addressObjectMaker.cpp \
          serviceObjectMaker.cpp \
          getProtoByName.cpp \
	  getServByName.cpp \
          Importer.cpp \
          IOSImporter.cpp \
          IOSImporterRun.cpp \
          IPTImporter.cpp \
          IPTImporterRun.cpp \
          PIXImporter.cpp \
          PIXImporterRun.cpp \

HEADERS	 = QStringListOperators.h \
           objectMaker.h \
           addressObjectMaker.h \
           serviceObjectMaker.h \
           getProtoByName.h \
	   getServByName.h \
           Importer.h \
           IOSImporter.h \
           IPTImporter.h \
           PIXImporter.h \

CONFIG += staticlib

INCLUDEPATH += $$ANTLR_INCLUDEPATH ../compiler_lib ../parsers ../libfwbuilder/src/

LIBS += $$ANTLR_LIBS
DEFINES += $$ANTLR_DEFINES

TARGET   = import

INSTALLS -= target
