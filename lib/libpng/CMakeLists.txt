#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# We only execute this script if the user wants to use our bundled libpng.
#
# It is built as a static relocatable library, and linked into any target that
# requires it.

# This only has effect in this directory (lib/png).
include_directories(include/)
if(WIN32)
	include_directories(../zlib/include/)
endif()

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

add_library(bundled_libpng STATIC include/png.h
                                  include/pngconf.h
                                  include/pnglibconf.h
                                  png.c
                                  pngdebug.h
                                  pngerror.c
                                  pngget.c
                                  pnginfo.h
                                  pngmem.c
                                  pngpread.c
                                  pngpriv.h
                                  pngread.c
                                  pngrio.c
                                  pngrtran.c
                                  pngrutil.c
                                  pngset.c
                                  pngstruct.h
                                  pngtrans.c
                                  pngwio.c
                                  pngwrite.c
                                  pngwtran.c
                                  pngwutil.c)

# Let consumers get at our bundled library in the standard CMake way.  These
# variables are not set in the cache, but instead shadow the variables in the
# cache.  (These are the same variables exported by the standard CMake FindPNG
# module).
set(PNG_LIBRARIES bundled_libpng PARENT_SCOPE)
set(PNG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/lib/libpng/include/ PARENT_SCOPE)

mark_as_advanced(PNG_LIBRARIES PNG_INCLUDE_DIRS)

# png calls deflate; say so, rather than relying on whoever links last to
# mention zlib afterwards. The Microsoft linker resolves across every archive it
# is given and does not care about order, so this was invisible on the platform
# that uses these bundled copies - but GNU ld walks the list once, left to
# right, and a cross-build for Windows fails on deflate, deflateEnd,
# deflateInit2_ and deflateReset with the libraries in the order CMake happened
# to emit them.
# Only when zlib is the bundled copy. On a system where zlib is found rather
# than built, bundled_zlib is not a target and naming it here becomes a literal
# -lbundled_zlib on the link line - which is how this went red on Linux the
# first time, while the cross-build it was written for was green.
if(TARGET bundled_zlib)
    target_link_libraries(bundled_libpng PUBLIC bundled_zlib)
endif()

# Build as position-independent code, since it will probably be linked into a
# dynamic library.
set_property(TARGET bundled_libpng PROPERTY POSITION_INDEPENDENT_CODE True)
