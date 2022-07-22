# - Find LIQUID
# Find the native LIQUID includes and library
# This module defines
#  LIQUID_INCLUDE_DIR, where to find LIQUID.h, etc.
#  LIQUID_LIBRARIES, the libraries needed to use LIQUID.
#  LIQUID_FOUND, If false, do not try to use LIQUID.
# also defined, but not for general use are
#  LIQUID_LIBRARY, where to find the LIQUID library.

FIND_PATH(LIQUID_INCLUDE_DIR liquid.h
        ${LIQUID_DIR}/include/liquid
        /opt/homebrew/include/liquid
        /opt/local/include/liquid
        /usr/include/liquid
        /usr/local/include/liquid
)

FIND_LIBRARY(LIQUID_LIBRARY
        NAMES liquid
        PATHS ${LIQUID_DIR}/lib
        "${LIQUID_DIR}\\win32\\lib"
        /opt/homebrew/lib
        /opt/local/lib
        /usr/lib64
        /usr/lib
        /usr/local/lib
        NO_DEFAULT_PATH
)

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