cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Buffy the Vampire Slayer (Xbox, NTSC) - Static Recompilation
#
# Hosts: Windows (MSVC, D3D11), Linux (GCC, SDL2 + OpenGL) and Nintendo
# Switch homebrew (devkitA64 + libnx, built with switch/build.sh).
# ============================================================================

project(buffy_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Buffy's image runs to 0x920D40; the runtime's fixed low areas (stack at
# 0x780000 etc.) must start above it (xbox_memory_layout.h).
add_compile_definitions(XBOX_LOW_AREA_SHIFT=0x200000)

# Record every indirect-branch target (tools.recomp.icall_feedback); costs a
# little on each ICALL, so only for harvesting runs.
option(GAME_ICALL_FEEDBACK "Build with RECOMP_ICALL_FEEDBACK" OFF)
if(GAME_ICALL_FEEDBACK)
    add_compile_definitions(RECOMP_ICALL_FEEDBACK)
endif()

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# The lifted code is ~190 MB of C generated from the retail XBE. It is never
# committed; point this at wherever tools.recomp --gen-dir wrote it.
set(GAME_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/recomp" CACHE PATH
    "Directory holding the tools.recomp output (recomp_*.c, recomp_funcs.h)")

if(NOT EXISTS "${GAME_GEN_DIR}/recomp_funcs.h")
    message(FATAL_ERROR "No generated code in ${GAME_GEN_DIR}. Run tools.recomp "
                        "with --gen-dir, or pass -DGAME_GEN_DIR=<dir>.")
endif()

# The Vulkan renderer (xboxrecomp nv2a_vk) instead of OpenGL. On the Switch it
# links mesa-switch's static NVK (NVK_SDK) and builds buffyx-vulkan.nro.
option(GAME_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${GAME_VULKAN} CACHE BOOL "" FORCE)

# Profile-guided optimisation of the lifted code and recomp_manual.c (most of
# the CPU time). Not the runtime: on the Switch the exception handler that
# emulates MMIO runs runtime code, and instrumented code there faulted again
# inside the handler (gcov's indirect-call profiler) -- every thread looped.
#   GAME_PGO=generate  instrumented build; .gcda files go to
#                        sdmc:/switch/buffyx/pgo/ (flat, names relative to the
#                        build dir), written every 30 s by switch_nx.c
#   GAME_PGO=use       build with them; GAME_PGO_DIR = that folder, copied
set(GAME_PGO "" CACHE STRING "generate | use | empty")
set(GAME_PGO_DIR "" CACHE PATH "The copied pgo folder for GAME_PGO=use")
get_filename_component(GAME_REAL_BINARY_DIR "${CMAKE_BINARY_DIR}" REALPATH)
set(GAME_PGO_FLAGS "")
if(GAME_PGO STREQUAL "generate")
    set(GAME_PGO_FLAGS "-fprofile-generate=sdmc:/switch/buffyx/pgo"
        "-fprofile-prefix-path=${GAME_REAL_BINARY_DIR}" "-fprofile-update=prefer-atomic"
        # Value / indirect-call profiling keeps its state in TLS read through
        # tpidr_el0, which is 0 on Horizon: the first indirect call faulted.
        -fno-profile-values)
    add_link_options(-fprofile-generate)
    add_compile_definitions(GAME_PGO_GENERATE=1)
elseif(GAME_PGO STREQUAL "use")
    set(GAME_PGO_FLAGS "-fprofile-use=${GAME_PGO_DIR}"
        "-fprofile-prefix-path=${GAME_REAL_BINARY_DIR}" -fno-profile-values -fprofile-partial-training
        -fprofile-correction -Wno-missing-profile -Wno-error=coverage-mismatch -Wno-coverage-mismatch)
endif()

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${GAME_GEN_DIR}/*.c")

# Hand-written code that runs on the lifted register model: same TLS model,
# PGO and LTO as the generated code.
set(GAME_HAND_SOURCES src/recomp_manual.c src/buffy_overrides.c src/buffy_input.c generated/hooks/xdk_hooks_glue.c
    ${XBOXRECOMP_DIR}/src/game/xdk_hooks.c)

set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
    src/buffy_overrides.c
    src/buffy_input.c
    src/buffy_visibility.c
    src/buffy_rumble.c
    generated/hooks/xdk_hooks_glue.c
    ${XBOXRECOMP_DIR}/src/game/xdk_hooks.c
    src/switch_nx.c
    src/switch_host.c
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)
if(NOT WIN32 AND NOT NINTENDO_SWITCH)
    # again after nv2a_gl, which calls xbox_FramebufferSetKey (fb_present.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE xbox_video)
endif()

if(GAME_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GAME_VULKAN=1)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GAME_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11 dxgi dxguid xinput winmm dbghelp)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
    endif()
else()
    # The generated chunks are huge straight-line functions; -O1 keeps GCC's
    # memory and time per chunk sane while still allocating registers. The
    # Switch's CPU is the bottleneck there, so it gets -O2.
    if(NINTENDO_SWITCH)
        set(GAME_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
    else()
        set(GAME_GEN_OPT "-O1" CACHE STRING "Optimisation for the lifted code")
    endif()
    # GAME_LTO: link-time optimisation of the lifted code and its manual
    # overrides, so lifted callers inline each other and the native leaves
    # (_ftol2, ...). The runtime and renderers are compiled as usual.
    # GAME_LTO_JOBS parallel LTRANS jobs (memory: ~1-2 GB each).
    option(GAME_LTO "Link-time optimisation of the lifted code" OFF)
    set(GAME_LTO_JOBS "4" CACHE STRING "Parallel LTO link jobs")
    set(GAME_GEN_FLAGS "${GAME_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv;${GAME_PGO_FLAGS}")
    # Switch: the lifted code (and the overrides LTO merges with it) reads the
    # thread pointer from tpidr_el0, which every thread running it sets
    # (xbox_nx_set_el0_tp, win32_compat.c). GAME_EL0_TP=OFF: libnx's
    # __aarch64_read_tp call per access, as before.
    option(GAME_EL0_TP "Lifted code reads TLS through tpidr_el0 (Switch)" ON)
    if(NINTENDO_SWITCH AND GAME_EL0_TP)
        list(APPEND GAME_GEN_FLAGS "-mtp=el0")
        set_property(SOURCE ${GAME_HAND_SOURCES} APPEND PROPERTY COMPILE_OPTIONS "-mtp=el0")
    endif()
    if(GAME_PGO_FLAGS)
        set_property(SOURCE ${GAME_HAND_SOURCES} APPEND PROPERTY
            COMPILE_OPTIONS "${GAME_PGO_FLAGS}")
    endif()
    if(GAME_LTO)
        list(APPEND GAME_GEN_FLAGS "-flto=auto")
        set_property(SOURCE ${GAME_HAND_SOURCES} APPEND PROPERTY
            COMPILE_OPTIONS "-flto=auto;-fno-strict-aliasing;-fwrapv")
        target_link_options(${PROJECT_NAME} PRIVATE
            "-flto=${GAME_LTO_JOBS}" "-flto-partition=balanced" ${GAME_GEN_OPT})
    endif()
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${GAME_GEN_FLAGS}")
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── Nintendo Switch homebrew (devkitA64 + libnx) ────────────────
#
# Build with switch/build.sh. The NRO reads the extracted disc from
# sdmc:/switch/buffyx/game/ (never an ISO); logs and buffyx_env.txt live in
# sdmc:/switch/buffyx/.
if(NINTENDO_SWITCH)
    set(GAME_SWITCH_APP_NAME "Buffy the Vampire Slayer (Xbox recomp)" CACHE STRING
        "Title shown in the homebrew menu")
    set(GAME_SWITCH_AUTHOR "FOTLabs" CACHE STRING "Author shown in the homebrew menu")
    set(GAME_SWITCH_VERSION "1.0.0" CACHE STRING "Version shown in the homebrew menu")
    set(GAME_SWITCH_ICON "" CACHE FILEPATH
        "256x256 JPEG icon (libnx default when empty)")

    if(GAME_VULKAN)
        # No GL: SDL is only the audio output; NVK comes with nv2a_vk.
        target_link_libraries(${PROJECT_NAME} PRIVATE nx m)
    else()
        # switch-sdl2's GL path is EGL on switch-mesa.
        set(GAME_MESA_DIR "" CACHE PATH
            "Newer Mesa install (lib/ with libEGL.a...) instead of switch-mesa 20.1")
        if(GAME_MESA_DIR)
            file(GLOB GAME_MESA_LIBS "${GAME_MESA_DIR}/lib/lib*.a")
            list(FILTER GAME_MESA_LIBS EXCLUDE REGEX "lib(nvk|vulkan|GLESv1_CM)\\.a$")
            target_link_options(${PROJECT_NAME} BEFORE PRIVATE "-L${GAME_MESA_DIR}/lib")
            target_link_libraries(${PROJECT_NAME} PRIVATE
                "-Wl,--start-group" ${GAME_MESA_LIBS} "-Wl,--end-group"
                drm_nouveau z stdc++ nx m)
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau nx m)
        endif()
    endif()
    # SDL's audio thread gets a real-time priority and audren gaps are
    # counted (switch_nx.c, "SDL's audio thread").
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:--wrap=SDL_SYS_SetThreadPriority"
        "LINKER:--wrap=audrvVoiceAddWaveBuf"
        "LINKER:--wrap=threadCreate"
        "LINKER:--wrap=threadClose")
    set_property(SOURCE src/switch_nx.c APPEND PROPERTY
        INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/apu")
    # switch_nx.c's loading screen asks EGL for the on-screen size.
    target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{DEVKITPRO}/portlibs/switch/include")

    set(GAME_NACP "${CMAKE_CURRENT_BINARY_DIR}/buffyx.nacp")
    nx_generate_nacp(OUTPUT "${GAME_NACP}"
        NAME "${GAME_SWITCH_APP_NAME}"
        AUTHOR "${GAME_SWITCH_AUTHOR}"
        VERSION "${GAME_SWITCH_VERSION}")
    if(GAME_SWITCH_ICON)
        nx_create_nro(${PROJECT_NAME} NACP "${GAME_NACP}" ICON "${GAME_SWITCH_ICON}")
    else()
        nx_create_nro(${PROJECT_NAME} NACP "${GAME_NACP}")
    endif()
endif()
