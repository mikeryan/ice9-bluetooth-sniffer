# - Find USRP (via UHD)
# Find the native UHD includes and library
# This module defines
#  UHD_INCLUDE_DIR, where to find uhd.h, etc.
#  UHD_LIBRARIES, the libraries needed to use UHD.
#  UHD_FOUND, If false, do not try to use UHD.
# also defined, but not for general use are
#  UHD_LIBRARY, where to find the UHD library.

find_package(PkgConfig)
pkg_check_modules(PC_UHD QUIET uhd)

find_path(UHD_INCLUDE_DIR
    NAMES uhd/config.hpp
    HINTS
        ${UHD_DIR}/include
        ${PC_UHD_INCLUDEDIR}
        ${PC_UHD_INCLUDE_DIRS}
        /opt/homebrew/include
        /opt/local/include
        /usr/include
        /usr/local/include
)

find_library(UHD_LIBRARY
    NAMES uhd
    HINTS
        ${UHD_DIR}/lib
        ${PC_UHD_LIBDIR}
        ${PC_UHD_LIBRARY_DIRS}
        /opt/homebrew/lib
        /opt/local/lib
        /usr/lib
        /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UHD DEFAULT_MSG UHD_LIBRARY UHD_INCLUDE_DIR)

if (UHD_LIBRARY AND UHD_INCLUDE_DIR)
    set(UHD_LIBRARIES ${UHD_LIBRARY})
    set(UHD_FOUND "YES")
else (UHD_LIBRARY AND UHD_INCLUDE_DIR)
    set(UHD_FOUND "NO")
endif (UHD_LIBRARY AND UHD_INCLUDE_DIR)

if (UHD_FOUND)
    if (NOT UHD_FIND_QUIETLY)
        message(STATUS "Found UHD: ${UHD_LIBRARIES}")
    endif (NOT UHD_FIND_QUIETLY)
else (UHD_FOUND)
    if (UHD_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find UHD library")
    endif (UHD_FIND_REQUIRED)
endif (UHD_FOUND)

# Deprecated declarations.
get_filename_component(NATIVE_UHD_LIB_PATH ${UHD_LIBRARY} PATH)

mark_as_advanced(
        UHD_LIBRARY
        UHD_INCLUDE_DIR
)

set(UHD_INCLUDE_DIRS ${UHD_INCLUDE_DIR})
set(UHD_LIBRARIES ${UHD_LIBRARY})