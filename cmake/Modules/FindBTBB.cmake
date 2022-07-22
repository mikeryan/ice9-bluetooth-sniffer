# - Find BTBB
# Find the native BTBB includes and library
# This module defines
#  BTBB_INCLUDE_DIR, where to find BTBB.h, etc.
#  BTBB_LIBRARIES, the libraries needed to use BTBB.
#  BTBB_FOUND, If false, do not try to use BTBB.
# also defined, but not for general use are
#  BTBB_LIBRARY, where to find the BTBB library.


FIND_PATH(BTBB_INCLUDE_DIR btbb.h
        ${BTBB_DIR}/include
        /opt/homebrew/include
        /opt/local/include
        /usr/include
        /usr/local/include
)

FIND_LIBRARY(BTBB_LIBRARY
        NAMES btbb
        PATHS ${BTBB_DIR}/lib
        "${BTBB_DIR}\\win32\\lib"
        /opt/homebrew/lib
        /opt/local/lib
        /usr/lib64
        /usr/lib
        /usr/local/lib
        NO_DEFAULT_PATH
)

IF (BTBB_LIBRARY AND BTBB_INCLUDE_DIR)
    SET(BTBB_LIBRARIES ${BTBB_LIBRARY})
    SET(BTBB_FOUND "YES")
ELSE (BTBB_LIBRARY AND BTBB_INCLUDE_DIR)
    SET(BTBB_FOUND "NO")
ENDIF (BTBB_LIBRARY AND BTBB_INCLUDE_DIR)

IF (BTBB_FOUND)
    IF (NOT BTBB_FIND_QUIETLY)
        MESSAGE(STATUS "Found libBTBB: ${BTBB_LIBRARIES}")
    ENDIF (NOT BTBB_FIND_QUIETLY)
ELSE (BTBB_FOUND)
    IF (BTBB_FIND_REQUIRED)
        MESSAGE(FATAL_ERROR "Could not find libBTBB library")
    ENDIF (BTBB_FIND_REQUIRED)
ENDIF (BTBB_FOUND)

# Deprecated declarations.
GET_FILENAME_COMPONENT (NATIVE_BTBB_LIB_PATH ${BTBB_LIBRARY} PATH)

MARK_AS_ADVANCED(
        BTBB_LIBRARY
        BTBB_INCLUDE_DIR
)