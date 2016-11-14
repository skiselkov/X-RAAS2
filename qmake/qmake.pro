# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END

# Copyright 2016 Saso Kiselkov. All rights reserved.

# Shared library without any Qt functionality
TEMPLATE = lib
QT -= gui core

CONFIG += warn_on plugin release
CONFIG -= thread exceptions qt rtti debug

VERSION = 1.0.0

INCLUDEPATH += ../FreeType/freetype-2.7/include
INCLUDEPATH += ../SDK/CHeaders/XPLM ../SDK/CHeaders/Widgets

QMAKE_CFLAGS += -std=c99 -g -W -Wall -Wextra -Werror -fvisibility=hidden
QMAKE_CFLAGS += -Wunused-result

# _GNU_SOURCE needed on Linux for getline()
# DEBUG - used by our ASSERT macro
# _FILE_OFFSET_BITS=64 to get 64-bit ftell and fseek on 32-bit platforms.
DEFINES += _GNU_SOURCE DEBUG _FILE_OFFSET_BITS=64

# Latest X-Plane APIs. No legacy support needed.
DEFINES += XPLM200 XPLM210

# Just a generally good idea not to depend on shipped libgcc.
LIBS += -static-libgcc

win32 {
	CONFIG += dll
	DEFINES += APL=0 IBM=1 LIN=0
	LIBS += -ldbghelp
	LIBS += -L../SDK/Libraries/Win
	TARGET = win.xpl
	INCLUDEPATH += ../OpenAL/include
	INCLUDEPATH += /usr/include/GL
	QMAKE_DEL_FILE = rm -f
}

win32:contains(CROSS_COMPILE, x86_64-w64-mingw32-) {
	LIBS += -lXPLM_64 -lXPWidgets_64
	LIBS += -L../OpenAL/libs/Win64 -lOpenAL32
	LIBS += -L../GL_for_Windows/lib -lopengl32
	LIBS += -L../FreeType/freetype-win-64/lib -lfreetype
}

win32:contains(CROSS_COMPILE, i686-w64-mingw32-) {
	LIBS += -lXPLM -lXPWidgets
	LIBS += -L../OpenAL/libs/Win32 -lOpenAL32
	LIBS += -L../GL_for_Windows/lib -lopengl32
	LIBS += -L../FreeType/freetype-win-32/lib -lfreetype
	DEFINES += __MIDL_user_allocate_free_DEFINED__
}

unix:!macx {
	DEFINES += APL=0 IBM=0 LIN=1
	TARGET = lin.xpl
	QMAKE_CFLAGS += `pkg-config --cflags openal`
	LIBS += `pkg-config --libs openal`
}

unix:!macx:contains(QMAKE_CFLAGS, -m64) {
	LIBS += -L../FreeType/freetype-linux-64/lib -lfreetype
}

unix:!macx:contains(QMAKE_CFLAGS, -m32) {
	LIBS += -L../FreeType/freetype-linux-32/lib -lfreetype
}

macx {
	DEFINES += APL=1 IBM=0 LIN=0
	TARGET = mac.xpl
	INCLUDEPATH += ../OpenAL/include
	QMAKE_LFLAGS += -F../SDK/Libraries/Mac
	QMAKE_LFLAGS += -framework XPLM -framework XPWidgets
	QMAKE_LFLAGS += -framework OpenGL -framework OpenAL

	LIBS += -L../FreeType/freetype-mac/lib -lfreetype
}

HEADERS += ../src/*.h ../api/c/XRAAS_ND_msg_decode.h
SOURCES += ../src/*.c ../api/c/XRAAS_ND_msg_decode.c
