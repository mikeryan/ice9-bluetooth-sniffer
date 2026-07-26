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
    set(VKFFT_ENABLE ON)
endif()
option(USE_VKFFT "Should VkFFT (GPU Acceleration) be used?" ${VKFFT_ENABLE})
option(BUILD_FFT_TESTS "Build FFT verification tests" OFF)

include(FetchContent)

# Prefer pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Add required dependencies
find_package(HackRF QUIET)
find_package(BladeRF QUIET)
find_package(UHD QUIET)
find_package(Liquid REQUIRED)

if (HACKRF_FOUND OR LIBHACKRF_FOUND OR HackRF_FOUND)
    set(HAVE_HACKRF TRUE)
    add_definitions(-DHAVE_HACKRF)
    message(STATUS "SDR support: HackRF ENABLED")
else()
    set(HAVE_HACKRF FALSE)
    message(STATUS "SDR support: HackRF DISABLED")
endif()

if (BLADERF_FOUND OR LIBBLADERF_FOUND OR BladeRF_FOUND)
    set(HAVE_BLADERF TRUE)
    add_definitions(-DHAVE_BLADERF)
    message(STATUS "SDR support: BladeRF ENABLED")
else()
    set(HAVE_BLADERF FALSE)
    message(STATUS "SDR support: BladeRF DISABLED")
endif()

if (UHD_FOUND OR Uhd_FOUND)
    set(HAVE_UHD TRUE)
    add_definitions(-DHAVE_UHD)
    message(STATUS "SDR support: USRP/UHD ENABLED")
else()
    set(HAVE_UHD FALSE)
    message(STATUS "SDR support: USRP/UHD DISABLED")
endif()

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
  DEPENDS ${PROJECT_SOURCE_DIR}/src/core/help.txt
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/core
  COMMENT "Generate Help Header"
  VERBATIM)


# Build RelWithDebInfo by default so builds are fast out of the box
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(SOURCES
    ${PROJECT_SOURCE_DIR}/src/protocol/bluetooth.c
    ${PROJECT_SOURCE_DIR}/src/protocol/btbb/btbb.c
    ${PROJECT_SOURCE_DIR}/src/dsp/burst_catcher.c
    ${PROJECT_SOURCE_DIR}/src/dsp/fsk.c
    ${PROJECT_SOURCE_DIR}/src/sdr/sdr_common.c
    ${PROJECT_SOURCE_DIR}/src/core/help.c
    ${PROJECT_SOURCE_DIR}/src/core/options.c
    ${PROJECT_SOURCE_DIR}/src/core/pcap.c
    ${PROJECT_SOURCE_DIR}/src/dsp/pfbch2.c
    ${PROJECT_SOURCE_DIR}/src/dsp/window.c

    ${PROJECT_SOURCE_DIR}/src/core/main.c
)

if(HAVE_HACKRF)
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/src/sdr/hackrf.c)
endif()

if(HAVE_BLADERF)
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/src/sdr/bladerf.c)
endif()

if(HAVE_UHD)
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/src/sdr/usrp.c)
endif()

if(USE_VKFFT)
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/vkfft/fft.cc)
else()
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/fftw/fft.c)
endif()

add_executable(ice9-bluetooth
    ${SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/help.h
)

set(SDR_INCLUDE_DIRS)
if(HAVE_HACKRF)
    list(APPEND SDR_INCLUDE_DIRS ${LIBHACKRF_INCLUDE_DIR})
endif()
if(HAVE_BLADERF)
    list(APPEND SDR_INCLUDE_DIRS ${LIBBLADERF_INCLUDE_DIR})
endif()
if(HAVE_UHD)
    list(APPEND SDR_INCLUDE_DIRS ${UHD_INCLUDE_DIR})
endif()

target_include_directories(ice9-bluetooth PRIVATE
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/dsp
    ${PROJECT_SOURCE_DIR}/src/sdr
    ${PROJECT_SOURCE_DIR}/src/protocol
    ${PROJECT_SOURCE_DIR}/src/protocol/btbb
    ${CMAKE_CURRENT_BINARY_DIR}
    ${SDR_INCLUDE_DIRS}
    ${LIQUID_INCLUDE_DIR}
)

if(USE_VKFFT)
  target_include_directories(ice9-bluetooth PRIVATE "vkfft")
  if(APPLE)
    set(DEFAULT_VKFFT_BACKEND 5)
  else()
    set(DEFAULT_VKFFT_BACKEND 0)
  endif()
  set(VKFFT_BACKEND ${DEFAULT_VKFFT_BACKEND} CACHE STRING "0 - Vulkan, 5 - Metal")
  add_definitions(-DVKFFT_BACKEND=${VKFFT_BACKEND})

  if(${VKFFT_BACKEND} EQUAL 5)
    add_compile_options(-WMTL_IGNORE_WARNINGS)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(QUARTZ_CORE_LIB QuartzCore REQUIRED)	
    find_library(METAL_LIB Metal REQUIRED)
    target_link_libraries(ice9-bluetooth PRIVATE
      ${FOUNDATION_LIB} ${QUARTZ_CORE_LIB} ${METAL_LIB})
    target_include_directories(ice9-bluetooth PRIVATE "metal-cpp/")
  elseif(${VKFFT_BACKEND} EQUAL 0)
    find_package(Vulkan REQUIRED)
    find_library(GLSLANG_LIB glslang REQUIRED)
    find_library(GLSLANG_RES_LIB glslang-default-resource-limits REQUIRED)
    find_library(SPIRV_LIB SPIRV REQUIRED)
    find_library(SPIRV_TOOLS_LIB SPIRV-Tools REQUIRED)
    find_library(SPIRV_TOOLS_OPT_LIB SPIRV-Tools-opt REQUIRED)
    target_link_libraries(ice9-bluetooth PRIVATE
      Vulkan::Vulkan ${GLSLANG_LIB} ${GLSLANG_RES_LIB} ${SPIRV_LIB} ${SPIRV_TOOLS_OPT_LIB} ${SPIRV_TOOLS_LIB})
    target_include_directories(ice9-bluetooth PRIVATE ${Vulkan_INCLUDE_DIRS} /usr/include/glslang/Include)
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

set(SDR_LIBRARIES)
if(HAVE_HACKRF)
    list(APPEND SDR_LIBRARIES ${LIBHACKRF_LIBRARIES})
endif()
if(HAVE_BLADERF)
    list(APPEND SDR_LIBRARIES ${LIBBLADERF_LIBRARIES})
endif()
if(HAVE_UHD)
    list(APPEND SDR_LIBRARIES ${UHD_LIBRARIES})
endif()

target_link_libraries(ice9-bluetooth
    PRIVATE Threads::Threads
    m
    ${SDR_LIBRARIES}
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
set(TEST_INCLUDES
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/dsp
    ${PROJECT_SOURCE_DIR}/src/sdr
    ${PROJECT_SOURCE_DIR}/src/protocol
    ${PROJECT_SOURCE_DIR}/src/protocol/btbb
)

add_executable(test_btbb
    tests/test_btbb.c
    src/protocol/btbb/btbb.c
)
target_include_directories(test_btbb PRIVATE ${TEST_INCLUDES})
target_compile_options(test_btbb PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_btbb PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_btbb PROPERTIES C_STANDARD 99)
add_test(NAME test_btbb COMMAND test_btbb)

add_executable(test_blocking_queue tests/test_blocking_queue.c)
target_include_directories(test_blocking_queue PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_blocking_queue PRIVATE Threads::Threads)
target_compile_options(test_blocking_queue PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_blocking_queue PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_blocking_queue PROPERTIES C_STANDARD 11)
add_test(NAME test_blocking_queue COMMAND test_blocking_queue)

add_executable(test_window tests/test_window.c src/dsp/window.c)
target_include_directories(test_window PRIVATE ${TEST_INCLUDES})
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

add_executable(test_pfbch2 tests/test_pfbch2.c src/dsp/pfbch2.c src/dsp/window.c)
target_include_directories(test_pfbch2 PRIVATE ${TEST_INCLUDES})
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

add_executable(test_fsk tests/test_fsk.c src/dsp/fsk.c)
target_include_directories(test_fsk PRIVATE ${TEST_INCLUDES} ${LIQUID_INCLUDE_DIR})
target_link_libraries(test_fsk PRIVATE m ${LIQUID_LIBRARIES})
target_compile_options(test_fsk PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_fsk PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_fsk PROPERTIES C_STANDARD 99)
add_test(NAME test_fsk COMMAND test_fsk)

add_executable(test_pcap tests/test_pcap.c src/core/pcap.c)
target_include_directories(test_pcap PRIVATE ${TEST_INCLUDES})
target_compile_options(test_pcap PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_pcap PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_pcap PROPERTIES C_STANDARD 99)
add_test(NAME test_pcap COMMAND test_pcap)

add_executable(test_options tests/test_options.c src/core/options.c src/core/pcap.c)
target_include_directories(test_options PRIVATE ${TEST_INCLUDES} ${SDR_INCLUDE_DIRS})
target_compile_options(test_options PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_options PRIVATE ${TEST_SANITIZER_FLAGS})
set_target_properties(test_options PROPERTIES C_STANDARD 99)
add_test(NAME test_options COMMAND test_options)

add_executable(test_sdr tests/test_sdr.c src/sdr/sdr_common.c src/core/options.c src/core/pcap.c)
target_include_directories(test_sdr PRIVATE ${TEST_INCLUDES} ${SDR_INCLUDE_DIRS})
target_compile_options(test_sdr PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_options(test_sdr PRIVATE ${TEST_SANITIZER_FLAGS})
target_link_libraries(test_sdr PRIVATE Threads::Threads)
set_target_properties(test_sdr PROPERTIES C_STANDARD 99)
add_test(NAME test_sdr COMMAND test_sdr)

if (USE_VKFFT AND BUILD_FFT_TESTS)
  add_executable(test_vkfft tests/test_vkfft.cpp vkfft/fft.cc)
  target_include_directories(test_vkfft PRIVATE ${TEST_INCLUDES} "vkfft")
  if(${VKFFT_BACKEND} EQUAL 5)
    target_link_libraries(test_vkfft PRIVATE ${FOUNDATION_LIB} ${QUARTZ_CORE_LIB} ${METAL_LIB} Threads::Threads)
    target_include_directories(test_vkfft PRIVATE "metal-cpp/")
  elseif(${VKFFT_BACKEND} EQUAL 0)
    target_link_libraries(test_vkfft PRIVATE Vulkan::Vulkan ${GLSLANG_LIB} ${GLSLANG_RES_LIB} ${SPIRV_LIB} ${SPIRV_TOOLS_OPT_LIB} ${SPIRV_TOOLS_LIB} Threads::Threads)
    target_include_directories(test_vkfft PRIVATE ${Vulkan_INCLUDE_DIRS} /usr/include/glslang/Include)
  endif()
  add_test(NAME test_vkfft COMMAND test_vkfft)

  find_package(FFTW REQUIRED)
  add_executable(test_fft_compare tests/test_fft_compare.cpp)
  target_include_directories(test_fft_compare PRIVATE ${TEST_INCLUDES} "vkfft" ${FFTW_INCLUDE_DIR})
  if(${VKFFT_BACKEND} EQUAL 5)
    target_link_libraries(test_fft_compare PRIVATE ${FOUNDATION_LIB} ${QUARTZ_CORE_LIB} ${METAL_LIB} FFTW::Float Threads::Threads)
    target_include_directories(test_fft_compare PRIVATE "metal-cpp/")
  elseif(${VKFFT_BACKEND} EQUAL 0)
    target_link_libraries(test_fft_compare PRIVATE Vulkan::Vulkan ${GLSLANG_LIB} ${GLSLANG_RES_LIB} ${SPIRV_LIB} ${SPIRV_TOOLS_OPT_LIB} ${SPIRV_TOOLS_LIB} FFTW::Float Threads::Threads)
    target_include_directories(test_fft_compare PRIVATE ${Vulkan_INCLUDE_DIRS} /usr/include/glslang/Include)
  endif()
  add_test(NAME test_fft_compare COMMAND test_fft_compare)
endif()
