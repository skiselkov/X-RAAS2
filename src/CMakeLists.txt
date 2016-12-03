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

cmake_minimum_required(VERSION 2.8.3)
project(plugin C)

SET(SRC xraas2.c geom.c math.c helpers.c perf.c log.c list.c avl.c conf.c
    riff.c wav.c airportdb.c rwy_key_tbl.c xraas_cfg.c snd_sys.c nd_alert.c
    dbg_gui.c ../api/c/XRAAS_ND_msg_decode.c gui.c init_msg.c text_rendering.c
    dr_intf.c)
SET(HDR geom.h math.h types.h helpers.h perf.h log.h list.h avl.h avl_impl.h
    conf.h riff.h wav.h airportdb.h rwy_key_tbl.h xraas_cfg.h snd_sys.h
    nd_alert.h dbg_gui.h ../api/c/XRAAS_ND_msg_decode.h gui.h init_msg.h
    text_rendering.h dr_intf.h)

SET(ALL_SRC ${SRC} ${HDR})
LIST(SORT ALL_SRC)

if(APPLE)
	add_executable(plugin ${ALL_SRC})
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g")
	SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -DDEBUG")
else()
	add_library(plugin SHARED ${ALL_SRC})
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g")
	# For some reason, we need to specify an optimization level here
	# or we won't get lstat/fstat correctly defined. Linux, WTF...
	SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O1 -g -DDEBUG")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror --std=c99")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
    -DCHECK_RESULT_USED=\"__attribute__ ((warn_unused_result))\"")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64")

#libraries
if(APPLE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -Wno-deprecated")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=10.7")
endif()
include_directories(plugin PUBLIC "../SDK/CHeaders/XPLM/"
    "../SDK/CHeaders/Widgets" "../OpenAL/include")

if (APPLE)
	include_directories(plugin PUBLIC
	    "../FreeType/freetype-mac-64/include/freetype2")
else()
	include_directories(plugin PUBLIC
	    "../FreeType/freetype-linux-64/include/freetype2")
endif()

# X-Plane stuff
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DXPLM200=1 -DXPLM210=1")
if(APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=1 -DIBM=0 -DLIN=0")
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=0 -DIBM=0 -DLIN=1")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")

#linking
link_libraries(m)
if(APPLE)
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ../SDK/Libraries/Mac)
	FIND_LIBRARY(XPLM_LIBRARY XPLM ../SDK/Libraries/Mac)
	FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets ../SDK/Libraries/Mac)
	FIND_LIBRARY(OPENAL_LIBRARY OpenAL)
	FIND_LIBRARY(OPENGL_LIBRARY OpenGL)
	# Can't use FIND_LIBRARY for FreeType, because it uses the system-level
	# dylib. Instead, we append it manually to the linker flags below.
	target_link_libraries(plugin ${XPLM_LIBRARY} ${XPWIDGETS_LIBRARY}
	    ${OPENAL_LIBRARY} ${OPENGL_LIBRARY})
else()
	FIND_LIBRARY(FREETYPE_LIBRARY freetype
	    "../FreeType/freetype-linux-64/lib")
	target_link_libraries(plugin ${FREETYPE_LIBRARY})
endif()

#output
SET_TARGET_PROPERTIES(plugin PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(plugin PROPERTIES SUFFIX "")
if(APPLE)
	set(PLUGIN_OUTPUT_FILENAME "mac.xpl")
	SET_TARGET_PROPERTIES(plugin PROPERTIES LINK_FLAGS
	    "${CMAKE_SHARED_LINKER_FLAGS} -fPIC -fvisibility=hidden -bundle \
	    -L../FreeType/freetype-mac-64/lib -lfreetype \
	    -mmacosx-version-min=10.7")
elseif(UNIX)
	set(PLUGIN_OUTPUT_FILENAME "lin.xpl")
	SET_TARGET_PROPERTIES(plugin PROPERTIES LINK_FLAGS
	    "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic -nodefaultlibs \
	    -undefined_warning -fPIC -fvisibility=hidden")
endif()

set_target_properties(plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_SOURCE_DIR}/bin/xraas2" )
set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_SOURCE_DIR}/bin/xraas2" )
set_target_properties(plugin PROPERTIES OUTPUT_NAME
    "${PLUGIN_OUTPUT_FILENAME}")
