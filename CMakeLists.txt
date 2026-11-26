cmake_minimum_required(VERSION 3.22)

project(et-5m-organ VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(et5m_dsp INTERFACE)
target_include_directories(et5m_dsp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(et5m_dsp INTERFACE cxx_std_20)

function(et5m_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endfunction()

option(ET5M_BUILD_TESTS "Build Catch2 DSP unit tests and plugin integration tests" ON)
option(ET5M_BUILD_PLUGIN "Build JUCE 8 VST3/AU/Standalone plugin and plugin tests" ON)

include(FetchContent)

if(ET5M_BUILD_PLUGIN)
    if(WIN32)
        set(ET5M_PLUGIN_FORMATS VST3 Standalone)
    elseif(APPLE)
        set(ET5M_PLUGIN_FORMATS AU VST3 Standalone)
    else()
        set(ET5M_PLUGIN_FORMATS VST3 Standalone)
    endif()

    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(ET5MOrgan
        COMPANY_NAME "Yuguen Organ"
        COMPANY_WEBSITE "https://github.com/yugen-tanaka"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE YgOr
        PLUGIN_CODE ET5M
        FORMATS ${ET5M_PLUGIN_FORMATS}
        PRODUCT_NAME "ET-5M Organ"
        BUNDLE_ID "com.yuguenorgan.et5m"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_1024.png"
        ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_256.png"
    )

    juce_generate_juce_header(ET5MOrgan)

    target_sources(ET5MOrgan PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
    )

    target_include_directories(ET5MOrgan PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_definitions(ET5MOrgan PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    if(MSVC)
        set_source_files_properties(
            src/plugin/PluginProcessor.cpp
            src/plugin/PluginEditor.cpp
            PROPERTIES COMPILE_OPTIONS "/W4"
        )
    else()
        set_source_files_properties(
            src/plugin/PluginProcessor.cpp
            src/plugin/PluginEditor.cpp
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wshadow"
        )
    endif()

    if(APPLE)
        target_compile_options(ET5MOrgan PRIVATE
            -Wno-unknown-warning-option
            -Wno-nontrivial-memcall
            -Wno-deprecated-declarations
        )
    endif()

    target_link_libraries(ET5MOrgan
        PRIVATE
            et5m_dsp
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()

if(ET5M_BUILD_TESTS)
    include(CTest)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    add_executable(et5m_dsp_tests tests/test_et5m_dsp.cpp)
    target_link_libraries(et5m_dsp_tests PRIVATE et5m_dsp Catch2::Catch2WithMain)
    et5m_apply_warnings(et5m_dsp_tests)
    catch_discover_tests(et5m_dsp_tests)

    if(ET5M_BUILD_PLUGIN)
        add_executable(et5m_plugin_tests tests/test_et5m_plugin.cpp)
        target_link_libraries(et5m_plugin_tests PRIVATE
            ET5MOrgan
            et5m_dsp
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_graphics
            Catch2::Catch2WithMain
        )
        target_include_directories(et5m_plugin_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        et5m_apply_warnings(et5m_plugin_tests)
        catch_discover_tests(et5m_plugin_tests)
    endif()
endif()
