# - Find USRP (via UHD)
# Find the native UHD includes and library
# This module defines
#  UHD_INCLUDE_DIR, where to find uhd.h, etc.
#  UHD_LIBRARIES, the libraries needed to use UHD.
#  UHD_FOUND, If false, do not try to use UHD.
# also defined, but not for general use are
#  UHD_LIBRARY, where to find the UHD library.

FIND_PATH(UHD_INCLUDE_DIR uhd/config.hpp
        ${UHD_DIR}/include
        /opt/homebrew/include
        /opt/local/include
        /usr/include
        /usr/local/include
)

FIND_LIBRARY(UHD_LIBRARY uhd
        ${UHD_DIR}/lib
        /opt/homebrew/lib
        /opt/local/lib
        /usr/lib
        /usr/local/lib
)

IF (UHD_LIBRARY AND UHD_INCLUDE_DIR)
    SET(UHD_LIBRARIES ${UHD_LIBRARY})
    SET(UHD_FOUND "YES")
ELSE (UHD_LIBRARY AND UHD_INCLUDE_DIR)
    SET(UHD_FOUND "NO")
ENDIF (UHD_LIBRARY AND UHD_INCLUDE_DIR)

IF (UHD_FOUND)
    IF (NOT UHD_FIND_QUIETLY)
        MESSAGE(STATUS "Found UHD: ${UHD_LIBRARIES}")
    ENDIF (NOT UHD_FIND_QUIETLY)
ELSE (UHD_FOUND)
    IF (UHD_FIND_REQUIRED)
        MESSAGE(FATAL_ERROR "Could not find UHD library")
    ENDIF (UHD_FIND_REQUIRED)
ENDIF (UHD_FOUND)

# Deprecated declarations.
GET_FILENAME_COMPONENT (NATIVE_UHD_LIB_PATH ${UHD_LIBRARY} PATH)

MARK_AS_ADVANCED(
        UHD_LIBRARY
        UHD_INCLUDE_DIR
)
