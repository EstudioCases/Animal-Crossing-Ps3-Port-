cmake_minimum_required(VERSION 3.16)
project(acgc_ps3 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(ACGC_PS3_BOOTSTRAP_ONLY "Build only the PS3 bootstrap target" ON)

set(DECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_compile_definitions(
    TARGET_PS3
    VERSION=0
    F3DEX_GBI_2
    NDEBUG
    BUGFIXES
    _LANGUAGE_C
)

set(PS3_PLATFORM_SOURCES
    src/ps3_main.c
    src/ps3_platform.c
    src/ps3_settings.c
    src/ps3_gx.c
    src/ps3_vi.c
    src/ps3_pad.c
    src/ps3_audio.c
    src/ps3_os.c
    src/ps3_dvd.c
    src/ps3_disc.c
    src/ps3_card.c
    src/ps3_m_card.c
    src/ps3_save_bswap.c
    src/ps3_aram.c
    src/ps3_misc.c
    src/ps3_mtx.c
    src/ps3_stubs.c
    src/ps3_stubs_cpp.cpp
    ${DECOMP_ROOT}/src/static/dolphin/pad/Padclamp.c
)

set(PS3_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DECOMP_ROOT}/include
    ${DECOMP_ROOT}/src
    ${DECOMP_ROOT}
)

if(ACGC_PS3_BOOTSTRAP_ONLY)
    add_executable(acgc_ps3_bootstrap
        src/ps3_main.c
        src/ps3_platform.c
        src/ps3_settings.c
        src/ps3_gx.c
        src/ps3_audio.c
    )
    target_compile_definitions(acgc_ps3_bootstrap PRIVATE ACGC_PS3_BOOTSTRAP_ONLY)
    target_include_directories(acgc_ps3_bootstrap PRIVATE ${PS3_INCLUDE_DIRS})
    set(PS3_TARGET acgc_ps3_bootstrap)
else()
    file(GLOB_RECURSE GAME_C_SOURCES CONFIGURE_DEPENDS
        "${DECOMP_ROOT}/src/*.c"
    )
    file(GLOB_RECURSE GAME_CXX_SOURCES CONFIGURE_DEPENDS
        "${DECOMP_ROOT}/src/*.cpp"
    )

    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/AUS/.*")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/AUS/.*")

    foreach(dir gx os vi pad card dvd ai ar dsp exi si db mtx base gba amcstubs OdemuExi2 odenotstub)
        list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/dolphin/${dir}/.*")
        list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/dolphin/${dir}/.*")
    endforeach()

    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/TRK_MINNOW_DOLPHIN/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/Runtime\\.PPCEABI\\.H/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/MSL_C\\.PPCEABI\\.bare\\.H/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/MSL_C/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/Famicom/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/libultra/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/ReconfigBATs\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/executor\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/ef_effect_lib\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/m_card\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/osreport\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/libc64/malloc\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/libc64/sprintf\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/libc64/aprintf\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/jaudio_NES/internal/dsp_cardunlock\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/jaudio_NES/internal/dsp_GBAKey\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/furniture/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/src/data/model/obj_e_boat/.*")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/ac_npc_shop_common\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/ac_animal_logo_misc\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/m_item_debug\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/actor/npc/.*_anime\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX ".*/emu64_utility\\.c$")

    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/dolphin/.*")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/TRK_MINNOW_DOLPHIN/.*")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/Runtime\\.PPCEABI\\.H/.*")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/MSL_C\\.PPCEABI\\.bare\\.H/.*")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/Famicom/.*")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/emu64_print\\.cpp$")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/jsyswrapper_ext\\.cpp$")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX ".*/jsyswrapper_main\\.cpp$")

    set_source_files_properties(
        "${DECOMP_ROOT}/src/main.c"
        PROPERTIES COMPILE_DEFINITIONS "main=ac_entry"
    )
    set_source_files_properties(
        "${DECOMP_ROOT}/src/static/boot.c"
        PROPERTIES COMPILE_DEFINITIONS "main=boot_main"
    )
    set_source_files_properties(
        "${DECOMP_ROOT}/src/static/libforest/emu64/emu64.c"
        PROPERTIES LANGUAGE CXX
    )

    set_source_files_properties(${GAME_C_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w"
    )
    set_source_files_properties(${GAME_CXX_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w;-fpermissive"
    )

    add_executable(acgc_ps3
        ${PS3_PLATFORM_SOURCES}
        ${GAME_C_SOURCES}
        ${GAME_CXX_SOURCES}
    )
    target_include_directories(acgc_ps3 PRIVATE ${PS3_INCLUDE_DIRS})
    target_compile_definitions(acgc_ps3 PRIVATE $<$<COMPILE_LANGUAGE:C>:nullptr=NULL>)
    set(PS3_TARGET acgc_ps3)
endif()

if(NOT MSVC)
    target_compile_options(${PS3_TARGET} PRIVATE
        -Wall
        -Wextra
        -mcpu=cell
        -mhard-float
        -fmodulo-sched
        -ffunction-sections
        -fdata-sections
        -fno-strict-aliasing
        -fwrapv
    )
    target_link_options(${PS3_TARGET} PRIVATE
        -mcpu=cell
        -mhard-float
        -Wl,--gc-sections
    )
endif()

target_link_libraries(${PS3_TARGET} PRIVATE
    gcm_sys
    rsx
    sysutil
    io
    audio
    rt
    lv2
    m
)

set_target_properties(${PS3_TARGET} PROPERTIES
    OUTPUT_NAME ACGCPS3
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

set(PS3_STAGE_DIR ${CMAKE_BINARY_DIR}/pkg)
add_custom_target(stage_ps3
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PS3_STAGE_DIR}/USRDIR
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${PS3_STAGE_DIR}/USRDIR
    DEPENDS ${PS3_TARGET}
)

find_program(MAKE_FSELF NAMES make_fself fself)
if(MAKE_FSELF)
    add_custom_command(
        OUTPUT ${PS3_STAGE_DIR}/USRDIR/EBOOT.BIN
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PS3_STAGE_DIR}/USRDIR
        COMMAND ${MAKE_FSELF} $<TARGET_FILE:${PS3_TARGET}> ${PS3_STAGE_DIR}/USRDIR/EBOOT.BIN
        DEPENDS ${PS3_TARGET} stage_ps3
        COMMENT "Creating debug EBOOT.BIN"
    )
    add_custom_target(eboot_ps3 ALL DEPENDS ${PS3_STAGE_DIR}/USRDIR/EBOOT.BIN)
else()
    message(WARNING "make_fself/fself not found; CMake will only build the ELF.")
endif()
