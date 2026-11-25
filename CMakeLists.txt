cmake_minimum_required(VERSION 3.22)

if(APPLE)
    # 仕様 6.3: macOS 12 以降、Apple Silicon / Intel ユニバーサルバイナリ
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "macOS deployment target" FORCE)
    endif()
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
endif()

project(EasyEQAssistant VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(libs/JUCE)

juce_add_gui_app(EasyEQAssistant
    PRODUCT_NAME "Easy EQ assistant"
    COMPANY_NAME "Ryuto"
    BUNDLE_ID "com.ryuto.easyeqassistant"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/icon_1024.png"
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(EasyEQAssistant)

target_sources(EasyEQAssistant PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h
    src/Audio/AudioEngine.cpp
    src/Audio/AudioEngine.h
    src/Audio/PinkNoiseGenerator.h
    src/UI/LevelMeter.cpp
    src/UI/LevelMeter.h
    src/UI/TopBar.cpp
    src/UI/TopBar.h
    src/UI/BottomBar.cpp
    src/UI/BottomBar.h
    src/UI/Strings.h
    src/Core/Acoustics.h
)

target_compile_definitions(EasyEQAssistant PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(EasyEQAssistant PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
)

# ---- テスト ----

option(EASYEQ_BUILD_TESTS "Build unit tests" ON)

if(EASYEQ_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(EasyEQTests
        tests/AcousticsTests.cpp
    )
    target_include_directories(EasyEQTests PRIVATE src)
    target_link_libraries(EasyEQTests PRIVATE gtest_main)

    include(GoogleTest)
    gtest_discover_tests(EasyEQTests)
endif()
