cmake_minimum_required(VERSION 3.1)
project(ice9-bluetooth C)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules" ${CMAKE_MODULE_PATH})

# TODO: Move to Find*-Modules
set(CMAKE_C_FLAGS "-I/opt/homebrew/include -Wall -g -O0 -fsanitize=address")
set(CMAKE_EXE_LINKER_FLAGS "-L/opt/homebrew/lib -lliquid -lbtbb -lhackrf -lbladeRF -fsanitize=address")

# Use pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Generate Help-Text-Header
add_custom_command(
  OUTPUT help.h
  COMMAND xxd -i help.txt help.h
  DEPENDS help.txt
)

include_directories(${PROJECT_SOURCE_DIR})

set(SOURCES
    ${PROJECT_SOURCE_DIR}/bladerf.c
    ${PROJECT_SOURCE_DIR}/bluetooth.c
    ${PROJECT_SOURCE_DIR}/burst_catcher.c
    ${PROJECT_SOURCE_DIR}/fsk.c
    ${PROJECT_SOURCE_DIR}/hackrf.c
    ${PROJECT_SOURCE_DIR}/hash.c
    ${PROJECT_SOURCE_DIR}/help.c
    ${PROJECT_SOURCE_DIR}/options.c
    ${PROJECT_SOURCE_DIR}/pcap.c

    ${PROJECT_SOURCE_DIR}/main.c
)


add_executable(ice9-bluetooth
    ${SOURCES}
)

target_link_libraries(ice9-bluetooth PRIVATE Threads::Threads)