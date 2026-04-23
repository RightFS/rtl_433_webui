# PetaLinux ARM cross-compilation toolchain file
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain prefix - adjust to your PetaLinux sysroot
set(CROSS_COMPILE arm-linux-gnueabihf-)

find_program(CMAKE_C_COMPILER   ${CROSS_COMPILE}gcc)
find_program(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)

if(NOT CMAKE_C_COMPILER OR NOT CMAKE_CXX_COMPILER)
  message(WARNING "Cross compiler ${CROSS_COMPILE}gcc not found - toolchain may not work")
  set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
  set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# PetaLinux sysroot (override with -DCMAKE_SYSROOT=/path/to/sysroot)
if(NOT CMAKE_SYSROOT)
  message(STATUS "CMAKE_SYSROOT not set - set it to your PetaLinux sysroot path")
endif()

# ARM Cortex-A9 flags (common for Zynq-7000 series)
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard")
