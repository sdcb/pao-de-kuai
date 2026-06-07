cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for third-party dependencies")

project(PaoDeKuai VERSION 0.1.0 LANGUAGES C CXX RC)

option(PDK_BUILD_TESTS "Build rule tests" ON)

include(FetchContent)

if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    )
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake 3.22 does not know the CXX26 feature table yet. Keep the requested
# project setting on newer CMake, and use GCC's current C++26 switch locally.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
    set(CMAKE_CXX_STANDARD 26)
else()
    set(CMAKE_CXX_STANDARD 23)
endif()

FetchContent_Declare(
    cjson_src
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson_src)
set(BUILD_TESTING ON CACHE BOOL "" FORCE)

add_library(cjson_static STATIC ${cjson_src_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson_static PUBLIC ${cjson_src_SOURCE_DIR})
target_compile_definitions(cjson_static PRIVATE CJSON_HIDE_SYMBOLS)

set(PDK_CORE_SOURCES
    src/core/Tween.cpp
    src/rules/Card.cpp
    src/rules/Deck.cpp
    src/rules/HandPattern.cpp
    src/rules/MoveValidator.cpp
    src/rules/PaoDeKuaiRules.cpp
    src/rules/Scoring.cpp
    src/game/AiStrategy.cpp
    src/game/GameState.cpp
    src/game/RoundRecorder.cpp
    src/stats/AppSettings.cpp
    src/stats/StatStore.cpp
)

add_library(pdk_core STATIC ${PDK_CORE_SOURCES})
target_include_directories(pdk_core PUBLIC src)
target_link_libraries(pdk_core PUBLIC cjson_static)

set(PDK_APP_SOURCES
    src/app/App.cpp
    src/app/Dpi.cpp
    src/app/Window.cpp
    src/audio/AudioEngine.cpp
    src/audio/SoundCatalog.cpp
    src/audio/WavLoader.cpp
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
    src/scenes/SettingsScene.cpp
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
target_link_libraries(pdk_app PUBLIC pdk_core)
target_compile_definitions(pdk_app PUBLIC
    WINVER=0x0602
    _WIN32_WINNT=0x0602
    UNICODE
    _UNICODE
    NOMINMAX
)
target_link_libraries(pdk_app PUBLIC
    d2d1 dwrite windowscodecs xaudio2_8 ole32 uuid shlwapi
    mfplat mfreadwrite mfuuid gdi32 user32 comdlg32
)

foreach(target pdk_core pdk_app cjson_static)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_VERSION VERSION_LESS "3.25")
    target_compile_options(pdk_core PUBLIC -std=c++2c)
    target_compile_options(pdk_app PUBLIC -std=c++2c)
endif()

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
    FetchContent_Declare(
        doctest_src
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.2.zip
    )
    FetchContent_MakeAvailable(doctest_src)

    add_executable(rules_tests
        tests/rules_tests/RulesTests.cpp
    )
    target_link_libraries(rules_tests PRIVATE pdk_core doctest::doctest)
    target_compile_definitions(rules_tests PRIVATE
        DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
        PDK_ENABLE_TEST_HOOKS
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(rules_tests PRIVATE -static-libstdc++ -static-libgcc)
        if(CMAKE_VERSION VERSION_LESS "3.25")
            target_compile_options(rules_tests PRIVATE -std=c++2c)
        endif()
    endif()

    enable_testing()
    add_test(NAME rules_tests COMMAND rules_tests)
    add_test(NAME ui_scene_start COMMAND scene_viewer --scene start --screenshot ${CMAKE_BINARY_DIR}/ui-start.jpg --quality 75)
    add_test(NAME ui_scene_game_deal COMMAND scene_viewer --scene game --mock deal --screenshot ${CMAKE_BINARY_DIR}/ui-game-deal.jpg --quality 75)
    add_test(NAME ui_scene_game COMMAND scene_viewer --scene game --screenshot ${CMAKE_BINARY_DIR}/ui-game.jpg --quality 75)
    add_test(NAME ui_overlay_return_menu COMMAND scene_viewer --scene game --overlay return-menu --screenshot ${CMAKE_BINARY_DIR}/ui-return-menu.jpg --quality 75)
    add_test(NAME ui_overlay_result COMMAND scene_viewer --scene game --overlay result-win --screenshot ${CMAKE_BINARY_DIR}/ui-result.jpg --quality 75)
endif()
