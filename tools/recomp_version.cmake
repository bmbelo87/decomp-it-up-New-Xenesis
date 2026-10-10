# Gera <OUT>/recomp_version.h com a versão do projeto (RECOMP_VERSION).
#
# Regra (pedida pelo usuário): cada commit soma 1 no último número.
#     versão = <MAJOR_MINOR>.<PATCH_BASE + commits desde BASE_COMMIT>
# Mudanças ainda não commitadas acrescentam "+" no fim.
#
# Para abrir uma série nova (ex.: 0.4): no CMakeLists.txt, troque
# RECOMP_MAJOR_MINOR para "0.4", RECOMP_PATCH_BASE para -1 e RECOMP_BASE_COMMIT
# para o último commit da série anterior (o commit dessa troca vira o .0).
#
# Sem git, ou sem o commit base no histórico (clone raso), usa
# <MAJOR_MINOR>.<PATCH_BASE> com "?" no fim.
#
# Roda a cada build (alvo recomp_version); só regrava o arquivo se o texto mudou,
# para não recompilar nada à toa.
#
# Uso: cmake -DSRC=<repo> -DOUT=<pasta> -DMAJOR_MINOR=0.2 -DPATCH_BASE=4
#            -DBASE_COMMIT=<hash> -P recomp_version.cmake

set(_fallback ${PATCH_BASE})
if(_fallback LESS 0)
    set(_fallback 0)
endif()
set(_ver "${MAJOR_MINOR}.${_fallback}?")

find_program(_git git)
if(_git)
    execute_process(
        COMMAND ${_git} rev-list --count ${BASE_COMMIT}..HEAD
        WORKING_DIRECTORY ${SRC}
        OUTPUT_VARIABLE _count
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _rc)
    if(_rc EQUAL 0 AND _count MATCHES "^[0-9]+$")
        math(EXPR _patch "${PATCH_BASE} + ${_count}")
        set(_ver "${MAJOR_MINOR}.${_patch}")
        execute_process(
            COMMAND ${_git} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${SRC}
            OUTPUT_VARIABLE _dirty
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT "${_dirty}" STREQUAL "")
            set(_ver "${_ver}+")
        endif()
    endif()
endif()

set(_text "/* GERADO por tools/recomp_version.cmake a cada build - nao editar. */\n#define RECOMP_VERSION \"${_ver}\"\n")
set(_file "${OUT}/recomp_version.h")
set(_old "")
if(EXISTS "${_file}")
    file(READ "${_file}" _old)
endif()
if(NOT "${_old}" STREQUAL "${_text}")
    file(MAKE_DIRECTORY "${OUT}")
    file(WRITE "${_file}" "${_text}")
    message(STATUS "RECOMP_VERSION = ${_ver}")
endif()
