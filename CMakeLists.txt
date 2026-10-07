cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(PDK_MSVC_RUNTIME "MT" CACHE STRING "MSVC runtime library: MT or MD")
set_property(CACHE PDK_MSVC_RUNTIME PROPERTY STRINGS MT MD)
if(PDK_MSVC_RUNTIME STREQUAL "MT")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(PDK_MSVC_RUNTIME STREQUAL "MD")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    message(FATAL_ERROR "PDK_MSVC_RUNTIME must be MT or MD, got '${PDK_MSVC_RUNTIME}'.")
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for third-party dependencies")

# Toolchain paths arrive from the presets as `$env{PDK_MINGW_ROOT}/bin/...`, so a
# Windows-style environment value gives CMake a mixed-separator path
# ("D:\_\3rd\mingw\bin/gcc.exe").  CMake writes those into CMake*Compiler.cmake
# as a quoted string, where "\3" is an invalid escape and configuration fails.
# Normalising before project() keeps the cache files valid.
foreach(_pdk_path_var CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_RC_COMPILER)
    if(DEFINED ${_pdk_path_var})
        file(TO_CMAKE_PATH "${${_pdk_path_var}}" ${_pdk_path_var})
    endif()
endforeach()

# C is the primary language for src/ and is expected to compile as standard C17
# on both MSVC (/std:c17) and MinGW-w64 (-std=c17).  CXX stays because tests/
# and tests/scene_viewer are C++ (doctest), and RC stays for the embedded
# resources (rc.exe on MSVC, windres.exe on MinGW).
project(PaoDeKuai VERSION 1.2 LANGUAGES C CXX RC)

# MinGW toolchain smoke check.  gcc invokes <root>/<triple>/bin/as.exe, which
# needs libwinpthread-1.dll from <root>/bin; when that directory is missing from
# PATH the assembler dies with STATUS_DLL_NOT_FOUND (0xC0000135) and gcc prints
# *no* diagnostics at all -- it just returns 1.  That shows up much later as an
# inexplicable compile failure, so fail here with an actionable message instead.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(_pdk_smoke_src "${CMAKE_BINARY_DIR}/pdk-toolchain-smoke.c")
    set(_pdk_smoke_exe "${CMAKE_BINARY_DIR}/pdk-toolchain-smoke.exe")
    file(WRITE "${_pdk_smoke_src}" "int main(void) { return 0; }\n")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -std=c17 -o "${_pdk_smoke_exe}" "${_pdk_smoke_src}"
        RESULT_VARIABLE _pdk_smoke_result
        OUTPUT_VARIABLE _pdk_smoke_out
        ERROR_VARIABLE _pdk_smoke_err
    )
    if(NOT _pdk_smoke_result EQUAL 0)
        message(FATAL_ERROR
            "MinGW toolchain smoke check failed (exit ${_pdk_smoke_result}).\n"
            "The most common cause is that <mingw-root>/bin is not on PATH: gcc spawns\n"
            "<root>/<triple>/bin/as.exe, which needs libwinpthread-1.dll from <root>/bin,\n"
            "and exits with 0xC0000135 without printing any error of its own.\n"
            "Compiler: ${CMAKE_C_COMPILER}\n"
            "stdout: ${_pdk_smoke_out}\n"
            "stderr: ${_pdk_smoke_err}")
    endif()
    file(REMOVE "${_pdk_smoke_src}" "${_pdk_smoke_exe}")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PDK_BUILD_REVISION "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE PDK_GIT_DESCRIPTION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(PDK_GIT_DESCRIPTION)
        set(PDK_BUILD_REVISION "${PDK_GIT_DESCRIPTION}")
    endif()
endif()

option(PDK_BUILD_TESTS "Build rule tests" ON)

set(PDK_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    )
    add_compile_options("$<$<CONFIG:Release>:/Os>")
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # -finput-charset keeps the UTF-8 sources (including Chinese comments and UI
    # strings) readable on toolchains whose default is not UTF-8.
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:-finput-charset=UTF-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:-finput-charset=UTF-8>"
    )
    # The main release target is the MinGW UCRT build, so release is tuned for
    # size: -Os plus one function/data per section and a linking --gc-sections
    # pass.  -flto was measured to give nothing on this project (plan.md 2) and
    # is deliberately not enabled.  -s strips symbols; there is no separate
    # "release with symbols" configuration yet.
    set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")
    add_compile_options(
        "$<$<CONFIG:Release>:-ffunction-sections>"
        "$<$<CONFIG:Release>:-fdata-sections>"
    )
    add_link_options(
        "$<$<CONFIG:Release>:-Wl,--gc-sections>"
        "$<$<CONFIG:Release>:-s>"
    )
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake 3.22 does not know the CXX26 feature table yet. Newer CMake versions
# still do not map CXX26 to MSVC, while CXX23 selects MSVC's current preview
# standard switch.
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
    set(CMAKE_CXX_STANDARD 26)
else()
    set(CMAKE_CXX_STANDARD 23)
endif()

# Minimum supported system is Windows 10 (plan.md revision 2 dropped the Win8
# baseline so that IAudioClient3 and the UCRT API set can be used directly).
add_library(pdk_win32 INTERFACE)
target_compile_definitions(pdk_win32 INTERFACE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
    NOMINMAX
)
# The same import libraries on both toolchains; MinGW resolves them to its own
# lib*.a.  `uuid` is kept for parity even though every GUID comes from
# src/graphics/iids.c (INITGUID), which is what actually resolves them.
target_link_libraries(pdk_win32 INTERFACE
    d2d1 dwrite windowscodecs ole32 uuid shlwapi imm32 avrt
    mfplat mfreadwrite mfuuid gdi32 user32 dwmapi
)

add_library(cjson_static STATIC "${PDK_EXTERNAL_DIR}/cjson/cJSON.c")
target_include_directories(cjson_static PUBLIC "${PDK_EXTERNAL_DIR}/cjson")
target_compile_definitions(cjson_static PRIVATE CJSON_HIDE_SYMBOLS)
target_link_libraries(cjson_static PRIVATE pdk_win32)

add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
target_include_directories(doctest SYSTEM INTERFACE "${PDK_EXTERNAL_DIR}/doctest")

# src/ is pure C.  Every entry below is .c; the C++ remnants live in tests/.
# Files that are not converted yet keep their .cpp name and are moved here as
# each stage lands (see docs/c-port-conventions.md).
set(PDK_CORE_SOURCES
    src/core/Str.c
    src/core/Timer.c
    src/core/Tween.c
    src/core/WinFile.c
    src/rules/Card.c
    src/rules/Deck.c
    src/rules/HandPattern.c
    src/rules/MoveValidator.c
    src/rules/PaoDeKuaiRules.c
    src/rules/RuleText.c
    src/rules/Scoring.c
    src/game/AiStrategyInternal.c
    src/game/BasicAiStrategy.c
    src/game/GameState.cpp
    src/game/ExternalAiController.c
    src/game/LocalAiController.c
    src/game/RoundRecorder.c
    src/game/RoundTraceRecorder.c
    src/game/StrongAiStrategy.cpp
    src/game/AiPlayer.c
    src/game/AiStrategy.c
    src/game/TurnRecord.c
    src/stats/AppSettings.c
    src/stats/StatStore.c
)

add_library(pdk_core STATIC ${PDK_CORE_SOURCES})
target_include_directories(pdk_core PUBLIC src)
target_link_libraries(pdk_core PUBLIC cjson_static pdk_win32)
target_compile_definitions(pdk_core PRIVATE
    PDK_APP_VERSION="${PROJECT_VERSION}"
    PDK_BUILD_REVISION="${PDK_BUILD_REVISION}"
)

set(PDK_APP_SOURCES
    src/app/App.cpp
    src/app/Dpi.c
    src/app/ImeInput.c
    src/app/Window.cpp
    src/app/WindowChrome.c
    src/audio/AudioDecoder.c
    src/audio/AudioEngine.c
    src/audio/SoundCatalog.c
    src/graphics/D2DContext.c
    src/graphics/ProceduralTextures.c
    src/graphics/WicImageLoader.c
    src/graphics/iids.c
    src/resources/CardAtlasData.c

    src/resources/ResourceLoader.c
    src/scenes/GameScene.cpp
    src/scenes/HelpScene.cpp
    src/scenes/LoadingScene.cpp
    src/scenes/StartScene.cpp
    src/scenes/StatsScene.cpp
    src/overlays/ConfirmExitDialog.c
    src/overlays/AboutOverlay.c
    src/overlays/InvalidMoveToast.c
    src/overlays/ReturnToMenuOverlay.c
    src/overlays/RoundResultOverlay.cpp
    src/overlays/SettingsOverlay.cpp
    src/overlays/TalkBubbleOverlay.c
    src/overlays/TipOverlay.c
    src/ui/CardView.c
    src/ui/Icons.c
    src/ui/Inputs.c
    src/ui/Widgets.c
)

add_library(pdk_app STATIC ${PDK_APP_SOURCES})
target_include_directories(pdk_app PUBLIC src)
target_link_libraries(pdk_app PUBLIC pdk_core pdk_win32)

foreach(target pdk_core pdk_app cjson_static)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_VERSION VERSION_LESS "3.25")
    target_compile_options(pdk_core PUBLIC -std=c++2c)
    target_compile_options(pdk_app PUBLIC -std=c++2c)
endif()

# rc.exe dependency scanning does not see RCDATA/ICON payloads, so list them
# explicitly (rc.exe on MSVC scans the .rc, windres needs them too).
file(GLOB PDK_AUDIO_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/*.mp3")
set_source_files_properties(src/resources/resources.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/pao-de-kuai.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/poker-cards.png;${PDK_AUDIO_RESOURCES}"
)

add_executable(pao_de_kuai WIN32
    src/app/WinMain.cpp
    src/resources/resources.rc
)
target_link_libraries(pao_de_kuai PRIVATE pdk_app)

add_executable(scene_viewer
    tests/scene_viewer/SceneViewer.cpp
    src/resources/resources.rc
)
target_link_libraries(scene_viewer PRIVATE pdk_app)

foreach(target pao_de_kuai scene_viewer)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endforeach()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(pao_de_kuai PRIVATE -municode)
endif()

if(PDK_BUILD_TESTS)
    # ABI gate for the COM shim.  shim_vendored_types.c builds on both
    # toolchains (it only needs our generated headers); shim_layout_ref.c
    # additionally compares the flat PDK_* vtables against MinGW-w64's C-mode
    # SDK headers, which MSVC cannot parse at all.
    add_executable(shim_layout
        tests/shim_layout/shim_main.c
        tests/shim_layout/shim_vendored_types.c
    )
    target_link_libraries(shim_layout PRIVATE pdk_win32)
    target_include_directories(shim_layout PRIVATE src tests/shim_layout)
    if(NOT MSVC)
        target_sources(shim_layout PRIVATE tests/shim_layout/shim_layout_ref.c)
    endif()

    # Decodes every embedded sound effect and checks the 44.1 kHz contract and the
    # MP3 encoder-delay trim.  The WASAPI path itself still needs a human ear, but
    # this covers everything up to the mixer on every toolchain and architecture.
    add_executable(audio_decode
        tests/audio_decode/AudioDecodeTest.c
        src/resources/resources.rc
    )
    target_link_libraries(audio_decode PRIVATE pdk_app pdk_win32)

    add_executable(unit_tests
        tests/rules_tests/TestMain.cpp
        tests/rules_tests/RulesPatternTests.cpp
        tests/rules_tests/ScoringTests.cpp
        tests/rules_tests/GameStateTests.cpp
        tests/rules_tests/AiStrategyTests.cpp
        tests/rules_tests/DragSelectionTests.cpp
        tests/rules_tests/StatsTests.cpp
    )
    target_link_libraries(unit_tests PRIVATE pdk_core doctest::doctest pdk_win32)
    target_compile_definitions(unit_tests PRIVATE
        DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
        PDK_ENABLE_TEST_HOOKS
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(unit_tests PRIVATE -static-libstdc++ -static-libgcc)
        if(CMAKE_VERSION VERSION_LESS "3.25")
            target_compile_options(unit_tests PRIVATE -std=c++2c)
        endif()
    endif()

    enable_testing()
    add_test(NAME shim_layout COMMAND shim_layout)
    add_test(NAME audio_decode COMMAND audio_decode)
    add_test(NAME unit_tests COMMAND unit_tests)
    add_test(NAME ui_scene_start COMMAND scene_viewer --scene start --screenshot ${CMAKE_BINARY_DIR}/ui-start.jpg --quality 75)
    add_test(NAME ui_overlay_settings COMMAND scene_viewer --scene settings --screenshot ${CMAKE_BINARY_DIR}/ui-settings.jpg --quality 75)
    add_test(NAME ui_scene_game_deal COMMAND scene_viewer --scene game --mock deal --screenshot ${CMAKE_BINARY_DIR}/ui-game-deal.jpg --quality 75)
    add_test(NAME ui_scene_game_play COMMAND scene_viewer --scene game --mock midgame --screenshot ${CMAKE_BINARY_DIR}/ui-game-play.jpg --quality 75)
    add_test(NAME ui_overlay_result COMMAND scene_viewer --scene game --overlay result-win --screenshot ${CMAKE_BINARY_DIR}/ui-result.jpg --quality 75)
endif()
