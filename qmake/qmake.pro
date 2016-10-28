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

INCLUDEPATH += ../SDK/CHeaders/XPLM
INCLUDEPATH += ../SDK/CHeaders/Wrappers
INCLUDEPATH += ../SDK/CHeaders/Widgets
INCLUDEPATH += ../OpenAL/include

QMAKE_CFLAGS += -std=c99 -Wall -Wextra -Werror -fvisibility=hidden

DEFINES += _GNU_SOURCE
DEFINES += CHECK_RESULT_USED='__attribute__\\(\\(warn_unused_result\\)\\)'

# Defined to use X-Plane SDK 2.0 capabilities - no backward compatibility before 9.0
DEFINES += XPLM200

win32 {
	message(win32)
	CONFIG += dll    	
	DEFINES += APL=0 IBM=1 LIN=0
	LIBS += -L../SDK/Libraries/Win
	TARGET = win.xpl
	INCLUDEPATH += .
	LIBS +=  "-lsetupapi"
	QMAKE_DEL_FILE          = rm -f
	INCLUDEPATH += "../WinSDK/Include"
	LIBS += -static-libgcc -Wl,-Bstatic -lstdc++ -Wl,-Bdynamic
}

win32:contains(CROSS_COMPILE, x86_64-w64-mingw32-){
	message(win32cross64)
	LIBS += -L"../WinSDK/Lib/x64"
	LIBS += -lXPLM_64 -lXPWidgets_64
	LIBS += -L"../OpenAL/libs/Win64" -lOpenAL32
}

win32:contains(CROSS_COMPILE, i686-w64-mingw32-){
	message(win32cross32)
	LIBS += -L"../WinSDK/Lib"
	LIBS += -lXPLM -lXPWidgets
	LIBS += -L"../OpenAL/libs/Win32" -lOpenAL32
	DEFINES += __MIDL_user_allocate_free_DEFINED__
}

unix:!macx {
	DEFINES += APL=0 IBM=0 LIN=1
	TARGET = lin.xpl
	LIBS += `pkg-config --libs openal`
	# QMAKE_LFLAGS += -Wl,-rpath=./Resources/plugins/X-RAAS2/64
	# QMAKE_RPATH=
}

macx {
	DEFINES += APL=1 IBM=0 LIN=0
	TARGET = mac.xpl
	QMAKE_LFLAGS += -F../SDK/Libraries/Mac/ -framework XPWidgets
	QMAKE_LFLAGS += -framework XPLM -lOpenAL
}

HEADERS += ../src/*.h
SOURCES += ../src/*.c
