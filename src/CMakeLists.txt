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
    riff.c wav.c airportdb.c rwy_key_tbl.c)
SET(HDR geom.h math.h types.h helpers.h perf.h log.h list.h avl.h avl_impl.h
    conf.h riff.h wav.h airportdb.h rwy_key_tbl.h)

SET(ALL_SRC ${SRC} ${HDR})
LIST(SORT ALL_SRC)

if(APPLE)
	add_executable(plugin ${ALL_SRC})
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g")
	SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -DDEBUG")
elseif(UNIX)
	add_library(plugin SHARED ${ALL_SRC})
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g")
	SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb3 -DDEBUG")
elseif(WIN32)
	add_library(plugin MODULE ${ALL_SRC})
	SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MD")
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} /MD")
	SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MDd")
endif()

if(WIN32)
	set(CMAKE_C_FLAGS
	    "${CMAKE_C_FLAGS} -DCHECK_RESULT_USED=_Check_return_")
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror --std=c99")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
	    -DCHECK_RESULT_USED=\"__attribute__ ((warn_unused_result))\"")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
endif()

#libraries
#libraries
if(UNIX)
        include_directories(xtcas PUBLIC "../SDK/CHeaders/XPLM/"
            "../SDK/CHeaders/Widgets/" "../SDK/CHeaders/Wrappers/"
            "../OpenAL/include")
elseif(APPLE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64")
elseif(WIN32)
        target_include_directories(xtcas PUBLIC "..\\SDK\\CHeaders\\XPLM"
            "..\\SDK\\CHeaders\\Widgets" "..\\SDK\\CHeaders\\Wrappers"
            "..\\OpenAL\\include")
endif()

if(APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64")
endif(APPLE)

# X-Plane stuff
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DXPLM200=1 -DXPLM210=1")
if(APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=1 -DIBM=0 -DLIN=0")
elseif(UNIX)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=0 -DIBM=0 -DLIN=1")
elseif(WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=0 -DIBM=1 -DLIN=0 \
	    -D_CRT_SECURE_NO_WARNINGS /bigobj")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")

#linking
link_libraries(m)
if(APPLE)
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ../SDK/Libraries/Mac/)
	FIND_LIBRARY(XPLM_LIBRARY XPLM ../SDK/Libraries/Mac/)
	FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets ../SDK/Libraries/Mac/)
	FIND_LIBRARY(OPENAL_LIBRARY OpenAL)
	target_link_libraries(plugin ${XPLM_LIBRARY} ${XPWIDGETS_LIBRARY}
	    ${OPENAL_LIBRARY})
elseif(WIN32)
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH}
	    "..\\SDK\\Libraries\\Win\\")
	FIND_LIBRARY(XPLM_LIBRARY XPLM_64
	    "..\\SDK\\Libraries\\Win\\")
	FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets_64
	    "..\\SDK\\Libraries\\Win\\")
	target_link_libraries(plugin ${XPLM_LIBRARY}
	    ${XPWIDGETS_LIBRARY})
	target_link_libraries(plugin_custom_dataref
	    ${XPLM_LIBRARY} ${XPWIDGETS_LIBRARY})
endif()

#output
SET_TARGET_PROPERTIES(plugin PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(plugin PROPERTIES SUFFIX "")
if(APPLE)
	set(PLUGIN_OUTPUT_FILENAME "mac.xpl")
	SET_TARGET_PROPERTIES(plugin PROPERTIES LINK_FLAGS
	    "${CMAKE_SHARED_LINKER_FLAGS} -fPIC -fvisibility=hidden \
	    -bundle -framework OpenGL")
elseif(UNIX)
	set(PLUGIN_OUTPUT_FILENAME "lin.xpl")
	SET_TARGET_PROPERTIES(plugin PROPERTIES LINK_FLAGS
	    "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic -nodefaultlibs \
	    -undefined_warning -fPIC -fvisibility=hidden")
elseif(WIN32)
	set(PLUGIN_OUTPUT_FILENAME "win.xpl")
endif()

set_target_properties(plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_SOURCE_DIR}/bin/xraas2" )
set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_SOURCE_DIR}/bin/xraas2" )
set_target_properties(plugin PROPERTIES OUTPUT_NAME
    "${PLUGIN_OUTPUT_FILENAME}")
