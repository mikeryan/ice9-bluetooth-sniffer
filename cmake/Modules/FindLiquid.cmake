# - Find LIQUID
# Find the native LIQUID includes and library
# This module defines
#  LIQUID_INCLUDE_DIR, where to find LIQUID.h, etc.
#  LIQUID_LIBRARIES, the libraries needed to use LIQUID.
#  LIQUID_FOUND, If false, do not try to use LIQUID.
# also defined, but not for general use are
#  LIQUID_LIBRARY, where to find the LIQUID library.

find_package(PkgConfig)
pkg_check_modules(PC_LIQUID QUIET liquid)

FIND_PATH(LIQUID_INCLUDE_DIR
    NAMES liquid.h
    HINTS
        ${LIQUID_DIR}/include
        ${PC_LIQUID_INCLUDEDIR}
        ${PC_LIQUID_INCLUDE_DIRS}
        /opt/homebrew/include
        /home/linuxbrew/.linuxbrew/include
        /opt/local/include
        /usr/include
        /usr/local/include
    PATH_SUFFIXES liquid
)

FIND_LIBRARY(LIQUID_LIBRARY
    NAMES liquid
    HINTS
        $ENV{LIQUID_DIR}/lib
        ${PC_LIQUID_LIBDIR}
        ${PC_LIQUID_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Liquid DEFAULT_MSG LIQUID_LIBRARY LIQUID_INCLUDE_DIR)

IF (LIQUID_LIBRARY AND LIQUID_INCLUDE_DIR)
    SET(LIQUID_LIBRARIES ${LIQUID_LIBRARY})
    SET(LIQUID_FOUND "YES")
ELSE (LIQUID_LIBRARY AND LIQUID_INCLUDE_DIR)
    SET(LIQUID_FOUND "NO")
ENDIF (LIQUID_LIBRARY AND LIQUID_INCLUDE_DIR)

IF (LIQUID_FOUND)
    IF (NOT LIQUID_FIND_QUIETLY)
        MESSAGE(STATUS "Found liquid-sdr: ${LIQUID_LIBRARIES}")
    ENDIF (NOT LIQUID_FIND_QUIETLY)
ELSE (LIQUID_FOUND)
    IF (LIQUID_FIND_REQUIRED)
        MESSAGE(FATAL_ERROR "Could not find liquid-sdr library")
    ENDIF (LIQUID_FIND_REQUIRED)
ENDIF (LIQUID_FOUND)

# Deprecated declarations.
GET_FILENAME_COMPONENT (NATIVE_LIQUID_LIB_PATH ${LIQUID_LIBRARY} PATH)

MARK_AS_ADVANCED(
        LIQUID_LIBRARY
        LIQUID_INCLUDE_DIR
)

set(LIQUID_INCLUDE_DIRS ${LIQUID_INCLUDE_DIR})
set(LIQUID_LIBRARIES ${LIQUID_LIBRARY})