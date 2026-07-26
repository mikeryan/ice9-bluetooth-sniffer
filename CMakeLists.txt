cmake_minimum_required(VERSION 3.9)
project(ice9-bluetooth)

set (CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules" ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS_DEBUG "-ggdb3 -g3 -O0 -fsanitize=address")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -fsanitize=address")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -ggdb3 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address")

if (APPLE)
    set(VKFFT_ENABLE ON)
else()
    set(VKFFT_ENABLE OFF)
endif()
option(USE_VKFFT "Should VkFFT (GPU Acceleration) be used?" ${VKFFT_ENABLE})

include(FetchContent)

# Prefer pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Add required dependencies
find_package(HackRF REQUIRED)
find_package(BladeRF REQUIRED)
find_package(UHD REQUIRED)
find_package(Liquid REQUIRED)

# find Wireshark install dir
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine
                OUTPUT_VARIABLE COMPILER_ABI
                OUTPUT_STRIP_TRAILING_WHITESPACE)
find_path(EXTCAP_INSTALL_PATH extcap HINTS
    /lib/${COMPILER_ABI}/wireshark
    /usr/lib64/wireshark
    /usr/lib/wireshark
    /Applications/Wireshark.app/Contents/MacOS
)
if (NOT EXTCAP_INSTALL_PATH)
  message("Warning: Wireshark extcap path not found. If you have Wireshark installed, report a bug")
endif()

# Generate header for help
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/help.h"
  COMMAND xxd -i help.txt "${CMAKE_CURRENT_BINARY_DIR}/help.h"
  DEPENDS ${PROJECT_SOURCE_DIR}/help.txt
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Generate Help Header"
  VERBATIM)


# Build RelWithDebInfo by default so builds are fast out of the box
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(SOURCES
    ${PROJECT_SOURCE_DIR}/bladerf.c
    ${PROJECT_SOURCE_DIR}/bluetooth.c
    ${PROJECT_SOURCE_DIR}/btbb/btbb.c
    ${PROJECT_SOURCE_DIR}/burst_catcher.c
    ${PROJECT_SOURCE_DIR}/fsk.c
    ${PROJECT_SOURCE_DIR}/hackrf.c
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
    ${CMAKE_CURRENT_BINARY_DIR}/help.h
)

target_include_directories(ice9-bluetooth PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBHACKRF_INCLUDE_DIR}
    ${LIBBLADERF_INCLUDE_DIR}
    ${UHD_INCLUDE_DIR}
    ${LIQUID_INCLUDE_DIR}
)

if(USE_VKFFT)
  target_include_directories(ice9-bluetooth PRIVATE "vkfft")
  set(VKFFT_BACKEND 5 CACHE STRING "5 - Metal")
  add_definitions(-DVKFFT_BACKEND=${VKFFT_BACKEND})

  if(${VKFFT_BACKEND} EQUAL 5)
    add_compile_options(-WMTL_IGNORE_WARNINGS)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(QUARTZ_CORE_LIB QuartzCore REQUIRED)	
    find_library(METAL_LIB Metal REQUIRED)
    target_link_libraries(ice9-bluetooth PRIVATE
      ${FOUNDATION_LIB} ${QUARTZ_CORE_LIB} ${METAL_LIB})
    target_include_directories(ice9-bluetooth PRIVATE "metal-cpp/")
  endif()
else()
  add_definitions(-DUSE_FFTW)
  find_package(FFTW REQUIRED)
  target_link_libraries(ice9-bluetooth PRIVATE FFTW::Float)
  target_include_directories(ice9-bluetooth PRIVATE "fftw" ${FFTW_INCLUDE_DIR})
endif()

target_compile_options(ice9-bluetooth PRIVATE
    -fno-common
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Werror=implicit-function-declaration
)

if (UNIX AND NOT APPLE)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    target_compile_options(ice9-bluetooth PRIVATE -mfpu=neon)
  else()
    target_compile_options(ice9-bluetooth PRIVATE -msse4.1)
  endif()
endif()

target_link_libraries(ice9-bluetooth
    PRIVATE Threads::Threads
    m
    ${LIBHACKRF_LIBRARIES}
    ${LIBBLADERF_LIBRARIES}
    ${UHD_LIBRARIES}
    ${LIQUID_LIBRARIES}
)

set_target_properties(ice9-bluetooth PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

install(TARGETS ice9-bluetooth DESTINATION bin)

if (EXTCAP_INSTALL_PATH)
  if(POLICY CMP0087)
    cmake_policy(SET CMP0087 NEW)
  endif()
  install(CODE "
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E make_directory \$ENV{DESTDIR}${EXTCAP_INSTALL_PATH}/extcap/
      )
  ")
  install(CODE "
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_INSTALL_PREFIX}/bin/ice9-bluetooth \$ENV{DESTDIR}${EXTCAP_INSTALL_PATH}/extcap/ice9-bluetooth
      )
  ")
endif()

# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

# Testing
enable_testing()

set(TEST_SANITIZER_FLAGS "-fsanitize=thread,undefined" "-g" "-fno-omit-frame-pointer" "-fno-sanitize-recover=all")

add_executable(test_btbb
    test_btbb.c
    btbb/btbb.c
)
target_include_directories(test_btbb PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_options(test_btbb PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_btbb PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_btbb PROPERTIES C_STANDARD 99)
add_test(NAME test_btbb COMMAND test_btbb)

add_executable(test_blocking_queue test_blocking_queue.c)
target_include_directories(test_blocking_queue PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_blocking_queue PRIVATE Threads::Threads)
target_compile_options(test_blocking_queue PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_blocking_queue PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_blocking_queue PROPERTIES C_STANDARD 11)
add_test(NAME test_blocking_queue COMMAND test_blocking_queue)

add_executable(test_window test_window.c window.c)
target_include_directories(test_window PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_window PRIVATE m)
target_compile_options(test_window PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_window PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_window PROPERTIES C_STANDARD 99)
if (UNIX AND NOT APPLE)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    target_compile_options(test_window PRIVATE -mfpu=neon)
  else()
    target_compile_options(test_window PRIVATE -msse4.1)
  endif()
endif()
add_test(NAME test_window COMMAND test_window)

add_executable(test_pfbch2 test_pfbch2.c pfbch2.c window.c)
target_include_directories(test_pfbch2 PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_pfbch2 PRIVATE m)
target_compile_options(test_pfbch2 PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_pfbch2 PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_pfbch2 PROPERTIES C_STANDARD 99)
if (UNIX AND NOT APPLE)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    target_compile_options(test_pfbch2 PRIVATE -mfpu=neon)
  else()
    target_compile_options(test_pfbch2 PRIVATE -msse4.1)
  endif()
endif()
add_test(NAME test_pfbch2 COMMAND test_pfbch2)

add_executable(test_fsk test_fsk.c fsk.c)
target_include_directories(test_fsk PRIVATE ${PROJECT_SOURCE_DIR} ${LIQUID_INCLUDE_DIR})
target_link_libraries(test_fsk PRIVATE m ${LIQUID_LIBRARIES})
target_compile_options(test_fsk PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_fsk PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_fsk PROPERTIES C_STANDARD 99)
add_test(NAME test_fsk COMMAND test_fsk)

add_executable(test_pcap test_pcap.c pcap.c)
target_include_directories(test_pcap PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_options(test_pcap PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_pcap PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_pcap PROPERTIES C_STANDARD 99)
add_test(NAME test_pcap COMMAND test_pcap)
