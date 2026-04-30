# CMake toolchain file for PSL1GHT.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ppu)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED ENV{PSL1GHT})
    message(FATAL_ERROR "PSL1GHT environment variable is not set")
endif()

file(TO_CMAKE_PATH "$ENV{PSL1GHT}" PSL1GHT_ENV)
set(PSL1GHT "${PSL1GHT_ENV}" CACHE PATH "PSL1GHT SDK path")

if(DEFINED ENV{PS3DEV})
    file(TO_CMAKE_PATH "$ENV{PS3DEV}" PS3DEV_ENV)
    set(PS3DEV "${PS3DEV_ENV}" CACHE PATH "PS3DEV SDK root")
else()
    get_filename_component(PS3DEV "${PSL1GHT}/.." ABSOLUTE)
endif()

find_program(PS3_CC NAMES ppu-gcc powerpc64-ps3-elf-gcc REQUIRED)
find_program(PS3_CXX NAMES ppu-g++ powerpc64-ps3-elf-g++ REQUIRED)
find_program(PS3_AR NAMES ppu-ar powerpc64-ps3-elf-ar)
find_program(PS3_RANLIB NAMES ppu-ranlib powerpc64-ps3-elf-ranlib)
find_program(PS3_STRIP NAMES ppu-strip powerpc64-ps3-elf-strip)

set(CMAKE_C_COMPILER ${PS3_CC})
set(CMAKE_CXX_COMPILER ${PS3_CXX})
if(PS3_AR)
    set(CMAKE_AR ${PS3_AR})
endif()
if(PS3_RANLIB)
    set(CMAKE_RANLIB ${PS3_RANLIB})
endif()
if(PS3_STRIP)
    set(CMAKE_STRIP ${PS3_STRIP})
endif()

set(CMAKE_FIND_ROOT_PATH
    ${PSL1GHT}
    ${PS3DEV}
    ${PS3DEV}/ppu
    ${PS3DEV}/portlibs/ppu
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

include_directories(SYSTEM
    ${PSL1GHT}/ppu/include
    ${PSL1GHT}/ppu/include/simdmath
    ${PS3DEV}/ppu/include
    ${PS3DEV}/ppu/powerpc64-ps3-elf/include
    ${PS3DEV}/portlibs/ppu/include
)

link_directories(
    ${PSL1GHT}/ppu/lib
    ${PS3DEV}/ppu/powerpc64-ps3-elf/lib
    ${PS3DEV}/portlibs/ppu/lib
)
