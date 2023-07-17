# - Find LIBHACKRF
# Find the native HACKRF includes and library
# This module defines
#  LIBHACKRF_INCLUDE_DIR, where to find hackrf.h, etc.
#  LIBHACKRF_LIBRARIES, the libraries needed to use HACKRF.
#  LIBHACKRF_FOUND, If false, do not try to use HACKRF.
# also defined, but not for general use are
#  LIBHACKRF_LIBRARY, where to find the HACKRF library.

find_package(PkgConfig)
pkg_check_modules(PC_LIBHACKRF QUIET libhackrf)

find_path(LIBHACKRF_INCLUDE_DIR
    NAMES hackrf.h
    HINTS
        $ENV{LIBHACKRF_DIR}/include
        ${PC_LIBHACKRF_INCLUDEDIR}
        ${PC_LIBHACKRF_INCLUDE_DIRS}
        /opt/homebrew/include
        /opt/local/include
        /home/linuxbrew/.linuxbrew/include
        /usr/include
        /usr/local/include
     PATH_SUFFIXES libhackrf
)

find_library(LIBHACKRF_LIBRARY
    NAMES hackrf
    HINTS
        $ENV{LIBHACKRF_DIR}/lib
        ${PC_LIBHACKRF_LIBDIR}
        ${PC_LIBHACKRF_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HackRF DEFAULT_MSG LIBHACKRF_LIBRARY LIBHACKRF_INCLUDE_DIR)

if(LIBHACKRF_LIBRARY AND HACKRF_INCLUDE_DIR)
    set(LIBHACKRF_LIBRARIES ${LIBHACKRF_LIBRARY})
    set(LIBHACKRF_FOUND "YES")
else (LIBHACKRF_LIBRARY AND HACKRF_INCLUDE_DIR)
    set(LIBHACKRF_FOUND "NO")
endif(LIBHACKRF_LIBRARY AND HACKRF_INCLUDE_DIR)

if(LIBHACKRF_FOUND)
    if(NOT HACKRF_FIND_QUIETLY)
        message(STATUS "Found HackRF: ${LIBHACKRF_LIBRARIES}")
    endif(NOT HACKRF_FIND_QUIETLY)
else (LIBHACKRF_FOUND)
    if(HACKRF_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find HackRF library")
    endif(HACKRF_FIND_REQUIRED)
endif(LIBHACKRF_FOUND)

# Deprecated declarations.
get_filename_component(NATIVE_LIBHACKRF_LIB_PATH ${LIBHACKRF_LIBRARY} PATH)

mark_as_advanced(
        LIBHACKRF_LIBRARY
        LIBHACKRF_INCLUDE_DIR
)

set(LIBHACKRF_INCLUDE_DIRS ${LIBHACKRF_INCLUDE_DIR})
set(LIBHACKRF_LIBRARIES ${LIBHACKRF_LIBRARY})