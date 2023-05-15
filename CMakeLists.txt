cmake_minimum_required(VERSION 3.9)
project(ice9-bluetooth)

set (CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules" ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-common -Wall -Wextra -Wno-unused-parameter -std=c99 -Werror=implicit-function-declaration")
if (UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon")
endif()

set(CMAKE_C_FLAGS_DEBUG "-ggdb3 -g3 -O0 -fsanitize=address")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -fsanitize=address")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -ggdb3 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address")

option(USE_VKFFT "Should VkFFT (GPU Acceleration) be used?" ON)

include(FetchContent)

# Installation directory
set(EXTCAP_INSTALL_PATH $ENV{EXTCAP_INSTALL_PATH})
if(EXTCAP_INSTALL_PATH)
else()
  set(EXTCAP_INSTALL_PATH "$ENV{HOME}/.config/wireshark/extcap")
endif()

# Prefer pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Add required dependencies
find_package(HackRF REQUIRED)
find_package(BladeRF REQUIRED)
find_package(UHD REQUIRED)
find_package(Liquid REQUIRED)

# Generate header for help
add_custom_command(
  OUTPUT "help.h"
  COMMAND xxd -i help.txt help.h
  DEPENDS ${PROJECT_SOURCE_DIR}/help.txt
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Generate Help Header"
  VERBATIM)


# Build RelWithDebInfo by default so builds are fast out of the box
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${HACKRF_INCLUDE_DIR})
include_directories(${BLADERF_INCLUDE_DIR})
include_directories(${UHD_INCLUDE_DIR})
include_directories(${LIQUID_INCLUDE_DIR})

set(SOURCES
    ${PROJECT_SOURCE_DIR}/bladerf.c
    ${PROJECT_SOURCE_DIR}/bluetooth.c
    ${PROJECT_SOURCE_DIR}/btbb/btbb.c
    ${PROJECT_SOURCE_DIR}/burst_catcher.c
    ${PROJECT_SOURCE_DIR}/fsk.c
    ${PROJECT_SOURCE_DIR}/hackrf.c
    ${PROJECT_SOURCE_DIR}/hash.c
    ${PROJECT_SOURCE_DIR}/help.c
    ${PROJECT_SOURCE_DIR}/options.c
    ${PROJECT_SOURCE_DIR}/pcap.c
    ${PROJECT_SOURCE_DIR}/usrp.c
    ${PROJECT_SOURCE_DIR}/pfbch2.c
    ${PROJECT_SOURCE_DIR}/window.c

    ${PROJECT_SOURCE_DIR}/main.c
)

if(USE_VKFFT)
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/vkfft/fft.cc)
else()
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/fftw/fft.c)
endif()

add_executable(ice9-bluetooth
    ${SOURCES}
    help.h
)

if(USE_VKFFT)
  include_directories("vkfft")
  set(VKFFT_BACKEND 5 CACHE STRING "5 - Metal")
  add_definitions(-DVKFFT_BACKEND=${VKFFT_BACKEND})

  if(${VKFFT_BACKEND} EQUAL 5)
    add_compile_options(-WMTL_IGNORE_WARNINGS)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(QUARTZ_CORE_LIB QuartzCore REQUIRED)	
    find_library(METAL_LIB Metal REQUIRED)
    target_link_libraries(ice9-bluetooth PRIVATE
      ${FOUNDATION_LIB} ${QUARTZ_CORE_LIB} ${METAL_LIB})
    target_include_directories(${PROJECT_NAME} PUBLIC "metal-cpp/")
  endif()
else()
  include_directories("fftw")
  add_definitions(-DUSE_FFTW)
  find_package(FFTW REQUIRED)
  target_link_libraries(ice9-bluetooth PRIVATE FFTW::Float)
  include_directories(${FFTW_INCLUDE_DIR})
endif()

target_link_libraries(ice9-bluetooth
    PRIVATE Threads::Threads
    m
    ${HACKRF_LIBRARIES}
    ${BLADERF_LIBRARIES}
    ${UHD_LIBRARIES}
    ${LIQUID_LIBRARIES}
)

set_property(TARGET ice9-bluetooth PROPERTY C_STANDARD 99)
set_property(TARGET ice9-bluetooth PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

install(TARGETS ice9-bluetooth DESTINATION ${EXTCAP_INSTALL_PATH})

# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
