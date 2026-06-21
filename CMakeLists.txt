cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(PDK_MSVC_RUNTIME "MT" CACHE STRING "MSVC runtime library for non-VC-LTL MSVC builds: MT or MD")
set_property(CACHE PDK_MSVC_RUNTIME PROPERTY STRINGS MT MD)
if(PDK_MSVC_RUNTIME STREQUAL "MT")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(PDK_MSVC_RUNTIME STREQUAL "MD")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    message(FATAL_ERROR "PDK_MSVC_RUNTIME must be MT or MD, got '${PDK_MSVC_RUNTIME}'.")
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for third-party dependencies")

project(PaoDeKuai VERSION 0.1.0 LANGUAGES C CXX RC)

option(PDK_BUILD_TESTS "Build rule tests" ON)
option(PDK_USE_VC_LTL "Use vendored VC-LTL CRT libraries for smaller MSVC binaries when available" ON)

set(PDK_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    )
    add_compile_options("$<$<CONFIG:Release>:/Os>")
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

add_library(pdk_vc_ltl INTERFACE)
if(MSVC AND PDK_USE_VC_LTL)
    if(NOT PDK_MSVC_RUNTIME STREQUAL "MT")
        message(FATAL_ERROR "VC-LTL builds require PDK_MSVC_RUNTIME=MT.")
    endif()

    set(PDK_VC_LTL_VERSION "6.2.9200.0")
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" PDK_MSVC_ARCH)
    if(PDK_MSVC_ARCH STREQUAL "x64")
        set(PDK_VC_LTL_PLATFORM "x64")
    elseif(PDK_MSVC_ARCH STREQUAL "x86")
        set(PDK_VC_LTL_PLATFORM "Win32")
    endif()

    if(PDK_VC_LTL_PLATFORM)
        set(PDK_VC_LTL_ROOT "${PDK_EXTERNAL_DIR}/vc-ltl")
        set(PDK_VC_LTL_INCLUDE_DIR "${PDK_VC_LTL_ROOT}/TargetPlatform/header")
        set(PDK_VC_LTL_VERSION_INCLUDE_DIR "${PDK_VC_LTL_ROOT}/TargetPlatform/${PDK_VC_LTL_VERSION}/header")
        set(PDK_VC_LTL_BUILD_SOURCE_DIR "${CMAKE_BINARY_DIR}/vc-ltl-build/src")
        set(PDK_VC_LTL_COPY_STAMP "${CMAKE_BINARY_DIR}/vc-ltl-build/copied.stamp")
        set(PDK_VC_LTL_SHARED_UCRT_LIB "${PDK_VC_LTL_BUILD_SOURCE_DIR}/Tools/${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}/libucrt_shared.lib")
        set(PDK_VC_LTL_LIBRARY_DIR "${PDK_VC_LTL_BUILD_SOURCE_DIR}/TargetPlatform/${PDK_VC_LTL_VERSION}/lib/${PDK_VC_LTL_PLATFORM}")
        set(PDK_VC_LTL_UCRT_LIB "${PDK_VC_LTL_LIBRARY_DIR}/libucrt.lib")
        set(PDK_VC_LTL_VCRUNTIME_LIB "${PDK_VC_LTL_LIBRARY_DIR}/libvcruntime.lib")
        find_program(PDK_MSBUILD_EXECUTABLE NAMES MSBuild.exe msbuild.exe)

        if(EXISTS "${PDK_VC_LTL_ROOT}/Build.proj"
            AND EXISTS "${PDK_VC_LTL_ROOT}/Tools/LibMaker.exe"
            AND EXISTS "${PDK_VC_LTL_ROOT}/Tools/${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}/Build_libucrt_shared.cmd"
            AND PDK_MSBUILD_EXECUTABLE)
            file(GLOB_RECURSE PDK_VC_LTL_SOURCE_FILES CONFIGURE_DEPENDS
                "${PDK_VC_LTL_ROOT}/*"
            )
            list(FILTER PDK_VC_LTL_SOURCE_FILES EXCLUDE REGEX "\\.lib$")

            add_custom_command(
                OUTPUT "${PDK_VC_LTL_COPY_STAMP}"
                COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PDK_VC_LTL_BUILD_SOURCE_DIR}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${PDK_VC_LTL_BUILD_SOURCE_DIR}"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PDK_VC_LTL_ROOT}" "${PDK_VC_LTL_BUILD_SOURCE_DIR}"
                COMMAND "${CMAKE_COMMAND}" -E touch "${PDK_VC_LTL_COPY_STAMP}"
                DEPENDS ${PDK_VC_LTL_SOURCE_FILES}
                COMMENT "Copying vendored VC-LTL source for ${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}"
                VERBATIM
            )
            add_custom_command(
                OUTPUT "${PDK_VC_LTL_SHARED_UCRT_LIB}"
                COMMAND "${CMAKE_COMMAND}" -E chdir "${PDK_VC_LTL_BUILD_SOURCE_DIR}/Tools/${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}"
                    cmd /d /s /c "Build_libucrt_shared.cmd >nul"
                DEPENDS "${PDK_VC_LTL_COPY_STAMP}"
                COMMENT "Building VC-LTL libucrt_shared for ${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}"
                VERBATIM
            )
            add_custom_command(
                OUTPUT "${PDK_VC_LTL_VCRUNTIME_LIB}"
                COMMAND "${PDK_MSBUILD_EXECUTABLE}" "${PDK_VC_LTL_BUILD_SOURCE_DIR}/vcruntime.msvcrt/vcruntime.msvcrt.vcxproj"
                    /nologo /m /v:minimal /clp:ErrorsOnly
                    /p:Configuration=Static
                    /p:Platform=${PDK_VC_LTL_PLATFORM}
                    /p:WindowsTargetPlatformMinVersion=${PDK_VC_LTL_VERSION}
                    /p:PreferredToolArchitecture=x64
                    /p:SolutionDir=${PDK_VC_LTL_BUILD_SOURCE_DIR}/
                DEPENDS "${PDK_VC_LTL_COPY_STAMP}"
                COMMENT "Building VC-LTL libvcruntime for ${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}"
                VERBATIM
            )
            add_custom_command(
                OUTPUT "${PDK_VC_LTL_UCRT_LIB}"
                COMMAND "${PDK_MSBUILD_EXECUTABLE}" "${PDK_VC_LTL_BUILD_SOURCE_DIR}/ucrtbase.msvcrt/ucrtbase.msvcrt.vcxproj"
                    /nologo /m /v:minimal /clp:ErrorsOnly
                    /p:Configuration=Static
                    /p:Platform=${PDK_VC_LTL_PLATFORM}
                    /p:WindowsTargetPlatformMinVersion=${PDK_VC_LTL_VERSION}
                    /p:PreferredToolArchitecture=x64
                    /p:SolutionDir=${PDK_VC_LTL_BUILD_SOURCE_DIR}/
                DEPENDS "${PDK_VC_LTL_COPY_STAMP}" "${PDK_VC_LTL_SHARED_UCRT_LIB}"
                COMMENT "Building VC-LTL libucrt for ${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}"
                VERBATIM
            )
            add_custom_target(pdk_vc_ltl_build DEPENDS "${PDK_VC_LTL_UCRT_LIB}" "${PDK_VC_LTL_VCRUNTIME_LIB}")
            target_include_directories(pdk_vc_ltl SYSTEM BEFORE INTERFACE
                "${PDK_VC_LTL_INCLUDE_DIR}"
                "${PDK_VC_LTL_VERSION_INCLUDE_DIR}"
            )
            target_link_directories(pdk_vc_ltl BEFORE INTERFACE "${PDK_VC_LTL_LIBRARY_DIR}")
            target_compile_definitions(pdk_vc_ltl INTERFACE
                _LTL_ucrt_module_type=1
                _LTL_vcruntime_module_type=1
                _LTL_vccorlib_module_type=1
            )
            message(STATUS "VC-LTL enabled from source: ${PDK_VC_LTL_VERSION}/${PDK_VC_LTL_PLATFORM}")
        else()
            message(WARNING "VC-LTL requested but its source tree or MSBuild is missing; using normal MSVC /MT runtime.")
        endif()
    else()
        message(STATUS "VC-LTL is not enabled for MSVC target architecture '${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}'.")
    endif()
endif()

add_library(cjson_static STATIC "${PDK_EXTERNAL_DIR}/cjson/cJSON.c")
target_include_directories(cjson_static PUBLIC "${PDK_EXTERNAL_DIR}/cjson")
target_compile_definitions(cjson_static PRIVATE CJSON_HIDE_SYMBOLS)
target_link_libraries(cjson_static PRIVATE pdk_vc_ltl)

add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
target_include_directories(doctest SYSTEM INTERFACE "${PDK_EXTERNAL_DIR}/doctest")

set(PDK_CORE_SOURCES
    src/core/StringUtil.cpp
    src/core/Tween.cpp
    src/core/WinFile.cpp
    src/rules/Card.cpp
    src/rules/Deck.cpp
    src/rules/HandPattern.cpp
    src/rules/MoveValidator.cpp
    src/rules/PaoDeKuaiRules.cpp
    src/rules/RuleText.cpp
    src/rules/Scoring.cpp
    src/game/AiStrategy.cpp
    src/game/GameState.cpp
    src/game/LocalAiController.cpp
    src/game/RoundRecorder.cpp
    src/game/TurnRecord.cpp
    src/stats/AppSettings.cpp
    src/stats/StatStore.cpp
)

add_library(pdk_core STATIC ${PDK_CORE_SOURCES})
target_include_directories(pdk_core PUBLIC src)
target_link_libraries(pdk_core PUBLIC cjson_static pdk_vc_ltl crypt32)

add_library(pdk_ai STATIC
    src/ai/LlmAiController.cpp
    src/ai/LlmOffscreenExperiment.cpp
    src/ai/PdkAiClient.cpp
    src/ai/TurnRecord.cpp
    src/ai/WinHttpJsonClient.cpp
)
target_include_directories(pdk_ai PUBLIC src)
target_link_libraries(pdk_ai PUBLIC pdk_core cjson_static)
target_link_libraries(pdk_ai PRIVATE winhttp)
target_compile_definitions(pdk_ai PRIVATE
    WINVER=0x0602
    _WIN32_WINNT=0x0602
    UNICODE
    _UNICODE
    NOMINMAX
)

set(PDK_APP_SOURCES
    src/app/App.cpp
    src/app/Dpi.cpp
    src/app/Window.cpp
    src/audio/AudioDecoder.cpp
    src/audio/AudioEngine.cpp
    src/audio/SoundCatalog.cpp
    src/dialogs/SettingsDialog.cpp
    src/graphics/D2DContext.cpp
    src/graphics/SpriteAtlas.cpp
    src/graphics/TextRenderer.cpp
    src/graphics/WicImageLoader.cpp
    src/resources/CardAtlasData.cpp
    src/resources/IconAtlasData.cpp
    src/resources/ResourceLoader.cpp
    src/scenes/GameScene.cpp
    src/scenes/HelpScene.cpp
    src/scenes/LoadingScene.cpp
    src/scenes/StartScene.cpp
    src/scenes/StatsScene.cpp
    src/overlays/ConfirmExitDialog.cpp
    src/overlays/AboutOverlay.cpp
    src/overlays/InvalidMoveToast.cpp
    src/overlays/ReturnToMenuOverlay.cpp
    src/overlays/RoundResultOverlay.cpp
    src/overlays/TalkBubbleOverlay.cpp
    src/overlays/TipOverlay.cpp
)

add_library(pdk_app STATIC ${PDK_APP_SOURCES})
target_include_directories(pdk_app PUBLIC src)
target_link_libraries(pdk_app PUBLIC pdk_ai)
target_compile_definitions(pdk_app PUBLIC
    WINVER=0x0602
    _WIN32_WINNT=0x0602
    UNICODE
    _UNICODE
    NOMINMAX
)
target_link_libraries(pdk_app PUBLIC
    d2d1 dwrite windowscodecs xaudio2_8 ole32 uuid shlwapi comctl32
    mfplat mfreadwrite mfuuid gdi32 user32 comdlg32
)

if(TARGET pdk_vc_ltl_build)
    add_dependencies(cjson_static pdk_vc_ltl_build)
    add_dependencies(pdk_core pdk_vc_ltl_build)
    add_dependencies(pdk_ai pdk_vc_ltl_build)
    add_dependencies(pdk_app pdk_vc_ltl_build)
endif()

foreach(target pdk_core pdk_ai pdk_app cjson_static)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_VERSION VERSION_LESS "3.25")
    target_compile_options(pdk_core PUBLIC -std=c++2c)
    target_compile_options(pdk_ai PUBLIC -std=c++2c)
    target_compile_options(pdk_app PUBLIC -std=c++2c)
endif()

function(pdk_add_vc_ltl_link_dependency target)
    if(TARGET pdk_vc_ltl_build)
        add_dependencies(${target} pdk_vc_ltl_build)
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${PDK_VC_LTL_UCRT_LIB}"
            "${PDK_VC_LTL_VCRUNTIME_LIB}"
        )
    endif()
endfunction()

add_executable(pao_de_kuai WIN32
    src/app/WinMain.cpp
    src/resources/resources.rc
)
target_link_libraries(pao_de_kuai PRIVATE pdk_app)
pdk_add_vc_ltl_link_dependency(pao_de_kuai)

add_executable(scene_viewer
    tests/scene_viewer/SceneViewer.cpp
    src/resources/resources.rc
)
target_link_libraries(scene_viewer PRIVATE pdk_app)
pdk_add_vc_ltl_link_dependency(scene_viewer)

foreach(target pao_de_kuai scene_viewer)
    target_compile_definitions(${target} PRIVATE
        WINVER=0x0602
        _WIN32_WINNT=0x0602
        UNICODE
        _UNICODE
        NOMINMAX
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endforeach()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(pao_de_kuai PRIVATE -municode)
endif()

if(PDK_BUILD_TESTS)
    add_executable(unit_tests
        tests/rules_tests/TestMain.cpp
        tests/rules_tests/RulesPatternTests.cpp
        tests/rules_tests/ScoringTests.cpp
        tests/rules_tests/GameStateTests.cpp
        tests/rules_tests/AiStrategyTests.cpp
        tests/rules_tests/DragSelectionTests.cpp
        tests/rules_tests/StatsTests.cpp
        tests/ai_client_tests/AiClientUnitTests.cpp
    )
    target_link_libraries(unit_tests PRIVATE pdk_ai doctest::doctest)
    pdk_add_vc_ltl_link_dependency(unit_tests)
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

    add_executable(ai_client_tests
        tests/rules_tests/TestMain.cpp
        tests/ai_client_tests/AiClientIntegrationTests.cpp
    )
    target_link_libraries(ai_client_tests PRIVATE pdk_ai doctest::doctest)
    pdk_add_vc_ltl_link_dependency(ai_client_tests)
    target_compile_definitions(ai_client_tests PRIVATE
        DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(ai_client_tests PRIVATE -static-libstdc++ -static-libgcc)
        if(CMAKE_VERSION VERSION_LESS "3.25")
            target_compile_options(ai_client_tests PRIVATE -std=c++2c)
        endif()
    endif()

    enable_testing()
    add_test(NAME unit_tests COMMAND unit_tests)
    add_test(NAME ui_scene_start COMMAND scene_viewer --scene start --screenshot ${CMAKE_BINARY_DIR}/ui-start.jpg --quality 75)
    add_test(NAME ui_dialog_settings COMMAND scene_viewer --scene settings --screenshot ${CMAKE_BINARY_DIR}/ui-settings.jpg --quality 75)
    add_test(NAME ui_scene_game_deal COMMAND scene_viewer --scene game --mock deal --screenshot ${CMAKE_BINARY_DIR}/ui-game-deal.jpg --quality 75)
    add_test(NAME ui_overlay_result COMMAND scene_viewer --scene game --overlay result-win --screenshot ${CMAKE_BINARY_DIR}/ui-result.jpg --quality 75)
endif()
