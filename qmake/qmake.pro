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
INCLUDEPATH += ../SDK/CHeaders/XPLM ../SDK/CHeaders/Widgets ../SDK
# Always just use the shipped OpenAL headers for predictability.
# The ABI is X-Plane-internal and stable anyway.
INCLUDEPATH += ../OpenAL/include
INCLUDEPATH += $$[LIBACFUTILS]/src

QMAKE_CFLAGS += -std=c99 -g -W -Wall -Wextra -Werror -fvisibility=hidden
QMAKE_CFLAGS += -Wunused-result

# _GNU_SOURCE needed on Linux for getline()
# DEBUG - used by our ASSERT macro
# _FILE_OFFSET_BITS=64 to get 64-bit ftell and fseek on 32-bit platforms.
# _USE_MATH_DEFINES - sometimes helps getting M_PI defined from system headers
DEFINES += _GNU_SOURCE DEBUG _FILE_OFFSET_BITS=64 _USE_MATH_DEFINES

# Latest X-Plane APIs. No legacy support needed.
DEFINES += XPLM200 XPLM210

# Aircraft-specific defines
DEFINES += ACF_TYPE=$$[ACF_TYPE]

XRAAS_EMBED=$$[XRAAS_EMBED]
contains(XRAAS_EMBED, yes) {
	DEFINES += XRAAS_IS_EMBEDDED
}

# Just a generally good idea not to depend on shipped libgcc.
!macx {
	LIBS += -static-libgcc
}

win32 {
	CONFIG += dll
	DEFINES += APL=0 IBM=1 LIN=0
	TARGET = win.xpl
	INCLUDEPATH += /usr/include/GL
	QMAKE_DEL_FILE = rm -f
}

win32:contains(CROSS_COMPILE, x86_64-w64-mingw32-) {
	QMAKE_CFLAGS += $$system("$$[LIBACFUTILS]/pkg-config-deps win-64 \
	    --cflags")

	# This must go first so GCC finds the deps in the latter libraries
	LIBS += -L $$[LIBACFUTILS]/qmake/win64 -lacfutils
	LIBS += $$system("$$[LIBACFUTILS]/pkg-config-deps win-64 --libs")
	LIBS += -ldbghelp
	LIBS += -L../SDK/Libraries/Win -lXPLM_64 -lXPWidgets_64
	LIBS += -L../OpenAL/libs/Win64 -lOpenAL32
	LIBS += -L../GL_for_Windows/lib -lopengl32
	LIBS += -L../FreeType/freetype-win-64/lib -lfreetype
}

win32:contains(CROSS_COMPILE, i686-w64-mingw32-) {
	QMAKE_CFLAGS += $$system("$$[LIBACFUTILS]/pkg-config-deps win-32 \
	    --cflags")
	LIBS += -L $$[LIBACFUTILS]/qmake/win32 -lacfutils
	LIBS += $$system("$$[LIBACFUTILS]/pkg-config-deps win-32 --libs")
	LIBS += -ldbghelp
	LIBS += -L../SDK/Libraries/Win -lXPLM -lXPWidgets
	LIBS += -L../OpenAL/libs/Win32 -lOpenAL32
	LIBS += -L../GL_for_Windows/lib -lopengl32
	LIBS += -L../FreeType/freetype-win-32/lib -lfreetype
}

unix:!macx {
	DEFINES += APL=0 IBM=0 LIN=1
	TARGET = lin.xpl
	LIBS += -nodefaultlibs
}

linux-g++-64 {
	QMAKE_CFLAGS += $$system("$$[LIBACFUTILS]/pkg-config-deps linux-64 \
	    --cflags")
	LIBS += -L../FreeType/freetype-linux-64/lib -lfreetype
	LIBS += -L $$[LIBACFUTILS]/qmake/lin64 -lacfutils
	LIBS += $$system("$$[LIBACFUTILS]/pkg-config-deps linux-64 --libs")
}

linux-g++-32 {
	QMAKE_CFLAGS += $$system("$$[LIBACFUTILS]/pkg-config-deps linux-32 \
	    --cflags")
	# The stack protector forces us to depend on libc,
	# but we'd prefer to be static.
	QMAKE_CFLAGS += -fno-stack-protector
	LIBS += -fno-stack-protector
	LIBS += -L../FreeType/freetype-linux-32/lib -lfreetype
	LIBS += -L $$[LIBACFUTILS]/qmake/lin32 -lacfutils
	LIBS += $$system("$$[LIBACFUTILS]/pkg-config-deps linux-32 --libs")
	LIBS += -lssp_nonshared
}

macx {
	DEFINES += APL=1 IBM=0 LIN=0
	TARGET = mac.xpl
	INCLUDEPATH += ../OpenAL/include
	LIBS += -F../SDK/Libraries/Mac
	LIBS += -framework XPLM -framework XPWidgets
	LIBS += -framework OpenGL -framework OpenAL
}

macx-clang {
	QMAKE_CFLAGS += $$system("$$[LIBACFUTILS]/pkg-config-deps mac-64 \
	    --cflags")
	LIBS += -L../FreeType/freetype-mac-64/lib -lfreetype
	LIBS += -L $$[LIBACFUTILS]/qmake/mac64 -lacfutils
	LIBS += $$system("$$[LIBACFUTILS]/pkg-config-deps mac-64 --libs")
}

macx-clang-32 {
	QMAKE_CFLAGS += $$system("$$[LIBACFUTILS]/pkg-config-deps mac-32 \
	    --cflags")
	LIBS += -L../FreeType/freetype-mac-32/lib -lfreetype
	LIBS += -L$$[LIBACFUTILS]/qmake/mac32 -lacfutils
	LIBS += $$system("$$[LIBACFUTILS]/pkg-config-deps mac-32 --libs")
}

HEADERS += ../src/*.h ../api/c/XRAAS_ND_msg_decode.h
SOURCES += ../src/*.c ../api/c/XRAAS_ND_msg_decode.c
