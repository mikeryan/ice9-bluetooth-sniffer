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

find_path(LIQUID_INCLUDE_DIR
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

find_library(LIQUID_LIBRARY
    NAMES liquid
    HINTS
        $ENV{LIQUID_DIR}/lib
        ${PC_LIQUID_LIBDIR}
        ${PC_LIQUID_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Liquid DEFAULT_MSG LIQUID_LIBRARY LIQUID_INCLUDE_DIR)

if (LIQUID_LIBRARY AND LIQUID_INCLUDE_DIR)
    set(LIQUID_LIBRARIES ${LIQUID_LIBRARY})
    set(LIQUID_FOUND "YES")
else (LIQUID_LIBRARY AND LIQUID_INCLUDE_DIR)
    set(LIQUID_FOUND "NO")
endif (LIQUID_LIBRARY AND LIQUID_INCLUDE_DIR)

if (LIQUID_FOUND)
    if (NOT LIQUID_FIND_QUIETLY)
        message(STATUS "Found liquid-sdr: ${LIQUID_LIBRARIES}")
    endif (NOT LIQUID_FIND_QUIETLY)
else (LIQUID_FOUND)
    if (LIQUID_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find liquid-sdr library")
    endif (LIQUID_FIND_REQUIRED)
endif (LIQUID_FOUND)

# Deprecated declarations.
get_filename_component(NATIVE_LIQUID_LIB_PATH ${LIQUID_LIBRARY} PATH)

mark_as_advanced(
        LIQUID_LIBRARY
        LIQUID_INCLUDE_DIR
)

set(LIQUID_INCLUDE_DIRS ${LIQUID_INCLUDE_DIR})
set(LIQUID_LIBRARIES ${LIQUID_LIBRARY})