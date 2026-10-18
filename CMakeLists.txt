cmake_minimum_required(VERSION 3.10)

# CMAKE_MSVC_RUNTIME_LIBRARY (runtime estático no build de release) precisa
# da política CMP0091 antes do project().
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(Pumpy C)

# Triplet estático do vcpkg (ex.: x86-windows-static): SDL2, zlib e o runtime
# do C entram no Pumpy.exe (/MT) -> o release não precisa de nenhuma DLL.
if(MSVC AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# macOS: por padrão compila pacote universal (Apple Silicon + Intel)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquiteturas macOS (Universal)" FORCE)
endif()

# Copia o executável para a pasta do jogo depois de compilar (opcional):
#   cmake -DPUMPY_GAME_DIR="/caminho/Pump It Up" ..
set(PUMPY_GAME_DIR "" CACHE PATH "Pasta do jogo: o Pumpy é copiado para lá após o build")

# ---------------------------------------------------------------------------
# Dependências: SDL2 (janela/entrada/áudio), OpenGL + GLU, zlib.
#   macOS  : SDL2.framework (third_party/mac ou sistema), OpenGL e zlib do sistema
#   Linux  : apt install libsdl2-dev libgl-dev libglu1-mesa-dev zlib1g-dev
#   Windows: vcpkg install sdl2 zlib   (opengl32/glu32 já vêm com o Windows)
#            cmake -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ..
# ---------------------------------------------------------------------------
add_library(pumpy_deps INTERFACE)

find_package(SDL2 CONFIG QUIET)
find_package(OpenGL QUIET)
find_package(ZLIB QUIET)

set(_have_sdl2 FALSE)
if(APPLE)
    # No macOS procura pelo SDL2.framework universal (em third_party/mac ou sistema)
    find_library(SDL2_FRAMEWORK NAMES SDL2
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mac
            /Library/Frameworks
            ~/Library/Frameworks
    )
    if(SDL2_FRAMEWORK)
        get_filename_component(SDL2_FRAMEWORK_DIR "${SDL2_FRAMEWORK}" DIRECTORY)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${SDL2_FRAMEWORK}/Headers"
            "${CMAKE_CURRENT_BINARY_DIR}/include/SDL2"
        )
        target_include_directories(pumpy_deps INTERFACE
            ${SDL2_FRAMEWORK}/Headers
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )
        target_link_libraries(pumpy_deps INTERFACE "-F${SDL2_FRAMEWORK_DIR} -framework SDL2")
        set(_have_sdl2 TRUE)
    endif()
endif()

if(NOT _have_sdl2)
    if(TARGET SDL2::SDL2)
        target_link_libraries(pumpy_deps INTERFACE SDL2::SDL2)
        set(_have_sdl2 TRUE)
    elseif(TARGET SDL2::SDL2-static)
        target_link_libraries(pumpy_deps INTERFACE SDL2::SDL2-static)
        set(_have_sdl2 TRUE)
    elseif(SDL2_FOUND AND SDL2_LIBRARIES)
        target_include_directories(pumpy_deps INTERFACE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(pumpy_deps INTERFACE ${SDL2_LIBRARIES})
        set(_have_sdl2 TRUE)
    endif()
endif()

set(_have_gl FALSE)
if(APPLE)
    target_link_libraries(pumpy_deps INTERFACE "-framework OpenGL")
    set(_have_gl TRUE)
elseif(OpenGL_FOUND AND TARGET OpenGL::GL AND TARGET OpenGL::GLU)
    target_link_libraries(pumpy_deps INTERFACE OpenGL::GL OpenGL::GLU)
    set(_have_gl TRUE)
endif()

set(_have_zlib FALSE)
if(APPLE)
    target_link_libraries(pumpy_deps INTERFACE "-lz")
    set(_have_zlib TRUE)
elseif(TARGET ZLIB::ZLIB)
    target_link_libraries(pumpy_deps INTERFACE ZLIB::ZLIB)
    set(_have_zlib TRUE)
endif()

# Fallback só para Linux sem pacotes -dev: usa os headers vendorizados em
# third_party/linux e linka as bibliotecas de runtime (.so.N) direto.
if(NOT (_have_sdl2 AND _have_gl AND _have_zlib))
    if(UNIX AND NOT APPLE)
        message(WARNING "Pacotes -dev de SDL2/GL/GLU/zlib não encontrados: usando headers "
                        "vendorizados (third_party/linux) e as bibliotecas de runtime.")
        set(_libdirs /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib64 /usr/lib64 /usr/lib)
        target_include_directories(pumpy_deps INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/include/SDL2)
        foreach(_spec "SDL2;SDL2-2.0;libSDL2-2.0.so.0" "GL;GL;libGL.so.1" "GLU;GLU;libGLU.so.1" "ZLIB;z;libz.so.1")
            list(GET _spec 0 _id)
            list(GET _spec 1 _n1)
            list(GET _spec 2 _n2)
            find_library(PUMPY_LIB_${_id} NAMES ${_n1} ${_n2} PATHS ${_libdirs})
            if(NOT PUMPY_LIB_${_id})
                message(FATAL_ERROR "Biblioteca ${_n1} não encontrada. Instale os pacotes -dev "
                                    "(libsdl2-dev libgl-dev libglu1-mesa-dev zlib1g-dev).")
            endif()
            target_link_libraries(pumpy_deps INTERFACE ${PUMPY_LIB_${_id}})
        endforeach()
    else()
        message(FATAL_ERROR "Faltam dependências (SDL2 / OpenGL+GLU / zlib). No Windows use vcpkg: "
                            "vcpkg install sdl2 zlib, e passe o toolchain file do vcpkg ao CMake.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Fontes. O código é o mesmo em todas as plataformas (SDL2 + OpenGL); só os
# poucos helpers Win32 usados pelo jogo são reimplementados fora do Windows.
# ---------------------------------------------------------------------------
set(PLATFORM_SOURCES "")
if(NOT WIN32)
    list(APPEND PLATFORM_SOURCES src/platform_posix.c)
endif()

# DESATIVADO no Exceed: o exceed.exe le WAVE/ do disco, nada e embutido.
# WAVs embutidos no executável (o PUMPY.EXE original os guarda como recursos).
# tools/embed_waves.py converte WAVE/*.wav num .c com arrays de bytes.
# find_package(Python3 REQUIRED COMPONENTS Interpreter)
# file(GLOB WAVE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/WAVE/*.wav")
# set(WAVE_EMBEDDED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wave_embedded.c")
# add_custom_command(
#     OUTPUT "${WAVE_EMBEDDED_C}"
#     COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_waves.py"
#             "${CMAKE_CURRENT_SOURCE_DIR}/WAVE" "${WAVE_EMBEDDED_C}"
#     DEPENDS ${WAVE_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_waves.py"
#     COMMENT "Embutindo WAVE/*.wav"
#     VERBATIM)

add_executable(Pumpy
    src/main.c
    src/window.c
    src/warning.c
    src/logo.c
    src/menu.c
    src/song_select.c
    src/staff.c
    src/testbga.c
    src/movie.c
    src/intro.c
    src/highscore.c
    src/ir.c
    src/ir_password.c
    src/ir_mixtable.c
    src/nameinput.c
    src/select_core.c
    # src/station.c   # Exceed2 — desativado
    # src/zero_station.c  # Zero — desativado
    src/nx_station.c
    src/old_save.c
    # src/exceed_songs.c   (Exceed: tabelas do exceed.exe)
    # src/exceed2_songs.c   # Exceed2 — desativado
    # src/zero_songs.c     # Zero — desativado
    src/nx_songs.c
    src/select_classic.c
    src/nx_select.c
    src/service_menu.c
    src/coin.c
    src/debug_console.c
    src/ranking.c
    src/result.c
    src/loading.c
    src/gameplay.c
    src/game_option.c
    src/eeprom.c
    src/render.c
    src/texture.c
    src/audio.c
    src/input.c
    src/font.c
    src/nx_text.c
    src/nx_world.c
    src/nx_world_data.c
    src/nx_mission.c
    src/nx_rank.c
    src/nx_credit.c
    src/old_data.c
    src/nx_training.c
    src/nx_sports.c
    src/nx_font.c
    src/nx_font_data.c
    src/nx_training_data.c
    src/bga.c
    src/vsl.c
    src/df_resource.c
    src/util.c
    src/resource.c
    src/dog_table.c
    src/step.c
    src/song.c
    src/zlibinflate.c
    # src/xbrz.c        # UPSCALE (xBRZ) das texturas — DESATIVADO (30/09/2026)
    ${PLATFORM_SOURCES}
    # ${WAVE_EMBEDDED_C}   (desativado no Exceed)
)

target_include_directories(Pumpy PRIVATE include)

# Versão do projeto (RECOMP_VERSION): cada commit soma 1 no último número.
# Série atual: 0.3.x. A 0.3.0 é o primeiro commit depois da renomeação (0fd498a).
# era: série 0.2.x, RECOMP_PATCH_BASE 4, base no commit da release v0.2.4 (f525b0d).
# era: série 0.3.x, RECOMP_MAJOR_MINOR "0.3", base 0fd498a. A 0.4 abre com o USE OLD DATA SONGS (base fcfa521).
# Para abrir a próxima série (ex.: 0.4): RECOMP_MAJOR_MINOR "0.4", RECOMP_PATCH_BASE -1 e
# RECOMP_BASE_COMMIT = último commit da série anterior; o commit dessa troca vira o .0
# (ver tools/recomp_version.cmake).
# era: RECOMP_PATCH_BASE -1, base fcfa521. Depois da v0.4.4 (17be57d) entraram commits de fora
# da série (.recomp.json, PR #6); a 0.4.5 é o commit de release seguinte a e41ee29.
set(RECOMP_MAJOR_MINOR "0.4")
set(RECOMP_PATCH_BASE 4)    # o 1º commit depois da base é o .5
set(RECOMP_BASE_COMMIT "e41ee29")
set(RECOMP_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(recomp_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${RECOMP_GEN_DIR}
        -DMAJOR_MINOR=${RECOMP_MAJOR_MINOR} -DPATCH_BASE=${RECOMP_PATCH_BASE}
        -DBASE_COMMIT=${RECOMP_BASE_COMMIT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/recomp_version.cmake
    BYPRODUCTS ${RECOMP_GEN_DIR}/recomp_version.h
    COMMENT "Versao do projeto (RECOMP_VERSION)"
    VERBATIM)
add_dependencies(Pumpy recomp_version)
target_include_directories(Pumpy PRIVATE ${RECOMP_GEN_DIR})

target_compile_definitions(Pumpy PRIVATE
    _CRT_SECURE_NO_WARNINGS
    SDL_MAIN_HANDLED          # main() próprio, sem SDL2main (ver window.c: SDL_SetMainReady)
)

# libmpeg2 vendorizada (decodificador dos vídeos MOV2/MOV3 do NX)
add_library(mpeg2 STATIC
    third_party/libmpeg2/libmpeg2/alloc.c
    third_party/libmpeg2/libmpeg2/cpu_accel.c
    third_party/libmpeg2/libmpeg2/cpu_state.c
    third_party/libmpeg2/libmpeg2/decode.c
    third_party/libmpeg2/libmpeg2/header.c
    third_party/libmpeg2/libmpeg2/idct.c
    third_party/libmpeg2/libmpeg2/motion_comp.c
    third_party/libmpeg2/libmpeg2/slice.c
    third_party/libmpeg2/libmpeg2/convert/rgb.c
)
target_include_directories(mpeg2 PUBLIC
    third_party/libmpeg2/include
)
target_include_directories(mpeg2 PRIVATE
    third_party/libmpeg2/libmpeg2
)
target_compile_definitions(mpeg2 PRIVATE
    HAVE_CONFIG_H
    OPT_GENERIC
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(Pumpy PRIVATE pumpy_deps mpeg2)

if(MSVC)
    target_compile_options(Pumpy PRIVATE /W3)
    target_link_options(Pumpy PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    # 4 GB de endereço no Win32 rodando em Windows 64-bit (UPSCALE xBRZ ocupa muita textura)
    # Flags de segurança para conformidade com Windows SmartScreen / Defender (ASLR + DEP)
    target_link_options(Pumpy PRIVATE /LARGEADDRESSAWARE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
elseif(WIN32)
    target_link_options(Pumpy PRIVATE -mwindows -Wl,--dynamicbase -Wl,--nxcompat)
endif()

if(WIN32)
    target_link_libraries(Pumpy PRIVATE winmm)   # timeGetTime / timeBeginPeriod
    target_sources(Pumpy PRIVATE src/icon.rc)    # ícone do .exe (src/icon.ico, gerado de icon.png)
    if(MSVC)
        target_sources(Pumpy PRIVATE src/Pumpy.manifest)
    endif()
elseif(APPLE)
    target_link_libraries(Pumpy PRIVATE pthread)
else()
    target_link_libraries(Pumpy PRIVATE m pthread dl)
endif()

if(APPLE)
    set_target_properties(Pumpy PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Pumpy"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.pumpy.game"
        MACOSX_BUNDLE_BUNDLE_VERSION "${GAME_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${GAME_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks;@executable_path/Frameworks;@executable_path/Pumpy.app/Contents/Frameworks;@executable_path;/Library/Frameworks"
    )

    set(PUMPY_ICON_PATH "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mac/AppIcon.icns")
        set(PUMPY_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mac/AppIcon.icns")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icns")
        set(PUMPY_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icns")
    endif()

    if(PUMPY_ICON_PATH)
        set_source_files_properties("${PUMPY_ICON_PATH}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(Pumpy PRIVATE "${PUMPY_ICON_PATH}")
    endif()

    if(SDL2_FRAMEWORK)
        add_custom_command(TARGET Pumpy POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Pumpy>/../Frameworks
            COMMAND /usr/bin/ditto "${SDL2_FRAMEWORK}" $<TARGET_FILE_DIR:Pumpy>/../Frameworks/SDL2.framework
            COMMENT "Embutindo SDL2.framework em Pumpy.app/Contents/Frameworks"
            VERBATIM)
    endif()

    add_custom_command(TARGET Pumpy POST_BUILD
        COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:Pumpy>
        COMMENT "Assinando Pumpy.app (ad-hoc)"
        VERBATIM)

    # Copia o executável universal para a raiz do build como binário CLI avulso
    add_custom_command(TARGET Pumpy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Pumpy> "${CMAKE_CURRENT_BINARY_DIR}/Pumpy"
        COMMENT "Disponibilizando executável avulso universal em ${CMAKE_CURRENT_BINARY_DIR}/Pumpy"
        VERBATIM)

    # Alvo para gerar pacote zip limpo e distribuível para macOS
    add_custom_target(package_mac
        COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${CMAKE_CURRENT_BINARY_DIR}/Pumpy-macOS-Universal.zip" --format=zip
                "Pumpy.app" "Pumpy"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS Pumpy
        COMMENT "Gerando pacote universal macOS: Pumpy-macOS-Universal.zip"
        VERBATIM)
endif()

# Windows: leva a SDL2.dll junto (vcpkg/SDL2 dev com DLL).
if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET Pumpy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:Pumpy>
        VERBATIM)
endif()

if(PUMPY_GAME_DIR)
    if(APPLE)
        add_custom_command(TARGET Pumpy POST_BUILD
            COMMAND /usr/bin/ditto $<TARGET_BUNDLE_DIR:Pumpy> "${PUMPY_GAME_DIR}/Pumpy.app"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Pumpy> "${PUMPY_GAME_DIR}/Pumpy"
            COMMENT "Copiando Pumpy.app e executável para ${PUMPY_GAME_DIR}"
            VERBATIM)
    else()
        add_custom_command(TARGET Pumpy POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Pumpy> "${PUMPY_GAME_DIR}"
            COMMENT "Copiando Pumpy para ${PUMPY_GAME_DIR}"
            VERBATIM)
    endif()
endif()

# Assets (BGA, AUDIO, STEP, Stage.cfg, WAVE) são fornecidos pelo usuário com
# os arquivos originais do jogo; o executável espera o diretório de trabalho
# apontando para a pasta do jogo.
