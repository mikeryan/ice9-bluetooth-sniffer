# - Find LIBBLADERF
# Find the native LIBBLADERF includes and library
# This module defines
#  LIBBLADERF_INCLUDE_DIR, where to find bladerf.h, etc.
#  LIBBLADERF_LIBRARIES, the libraries needed to use LIBBLADERF.
#  LIBBLADERF_FOUND, If false, do not try to use LIBBLADERF.
# also defined, but not for general use are
#  LIBBLADERF_LIBRARY, where to find LIBBLADERF.

find_package(PkgConfig)
pkg_check_modules(PC_LIBHACKRF QUIET libbladeRF)

find_path(LIBBLADERF_INCLUDE_DIR
    NAMES libbladeRF.h
    HINTS
        ${LIBBLADERF_DIR}/include
        ${PYBOMBS_PREFIX}/include
        ${PC_LIBBLADERF_INCLUDEDIR}
        ${PC_LIBBLADERF_INCLUDE_DIRS}
        /opt/homebrew/include
        /opt/local/include
        /usr/include
        /usr/local/include
)

find_library(LIBBLADERF_LIBRARY
    NAMES bladeRF
    HINTS
        $ENV{LIBBLADERF_DIR}/lib
        ${PC_LIBBLADERF_LIBDIR}
        ${PYBOMBS_PREFIX}/lib
        ${PC_LIBBLADERF_LIBRARY_DIRS}
        /opt/homebrew/lib
        /opt/local/lib
        /usr/lib
        /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BladeRF DEFAULT_MSG LIBBLADERF_LIBRARY LIBBLADERF_INCLUDE_DIR)

if (LIBBLADERF_LIBRARY AND LIBBLADERF_INCLUDE_DIR)
    set(LIBBLADERF_LIBRARIES ${LIBBLADERF_LIBRARY})
    set(LIBBLADERF_FOUND "YES")
else (LIBBLADERF_LIBRARY AND LIBBLADERF_INCLUDE_DIR)
    set(LIBBLADERF_FOUND "NO")
endif (LIBBLADERF_LIBRARY AND LIBBLADERF_INCLUDE_DIR)

if (LIBBLADERF_FOUND)
    if (NOT LIBBLADERF_FIND_QUIETLY)
        message(STATUS "Found bladeRF: ${LIBBLADERF_LIBRARIES}")
    endif (NOT LIBBLADERF_FIND_QUIETLY)
else (LIBBLADERF_FOUND)
    if (LIBBLADERF_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find bladeRF library")
    endif (LIBBLADERF_FIND_REQUIRED)
endif (LIBBLADERF_FOUND)

# Deprecated declarations.
get_filename_component(NATIVE_LIBBLADERF_LIB_PATH ${LIBBLADERF_LIBRARY} PATH)

mark_as_advanced(
        LIBBLADERF_LIBRARY
        LIBBLADERF_INCLUDE_DIR
)

set(LIBBLADERF_INCLUDE_DIRS ${LIBBLADERF_INCLUDE_DIR})
set(LIBBLADERF_LIBRARIES ${LIBBLADERF_LIBRARY})
