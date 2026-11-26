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

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # ソースと実行文字セットを UTF-8 に統一する（日本語を含むソース/リテラルのため。仕様 2.）
    add_compile_options(/utf-8)
endif()

add_subdirectory(libs/JUCE)

juce_add_gui_app(EasyEQAssistant
    # 実行ファイル名に空白を含めない（CFBundleExecutable "Easy EQ assistant" のままだと
    # TCC の帰属処理が壊れ、マイク許可ダイアログが出ず自動拒否される）
    PRODUCT_NAME "EasyEQAssistant"
    COMPANY_NAME "Ryuto"
    BUNDLE_ID "com.ryuto.easyeqassistant"
    PLIST_TO_MERGE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/display_name.plist"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/icon_1024.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "このアプリは、スピーカー系の周波数特性を測定するためにマイク入力を使用します。"
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_include_directories(EasyEQAssistant PRIVATE src)

target_sources(EasyEQAssistant PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h
    src/Audio/AudioEngine.cpp
    src/Audio/AudioEngine.h
    src/Audio/PinkNoiseGenerator.h
    src/DSP/SampleRingBuffer.h
    src/DSP/TransferFunctionAccumulator.cpp
    src/DSP/TransferFunctionAccumulator.h
    src/DSP/MeasurementEngine.cpp
    src/DSP/MeasurementEngine.h
    src/DSP/DelayEstimator.cpp
    src/DSP/DelayEstimator.h
    src/DSP/ImpulseResponseBuilder.cpp
    src/DSP/ImpulseResponseBuilder.h
    src/DSP/EQProposer.cpp
    src/DSP/EQProposer.h
    src/DSP/RtaSplAnalyzer.cpp
    src/DSP/RtaSplAnalyzer.h
    src/DSP/OctaveSmoother.h
    src/DSP/PEQ.h
    src/DSP/SweepGenerator.h
    src/DSP/Waterfall.h
    src/UI/LevelMeter.cpp
    src/UI/LevelMeter.h
    src/UI/TopBar.cpp
    src/UI/TopBar.h
    src/UI/BottomBar.cpp
    src/UI/BottomBar.h
    src/UI/FrequencyGraphView.cpp
    src/UI/FrequencyGraphView.h
    src/UI/MeasurementPanel.cpp
    src/UI/MeasurementPanel.h
    src/UI/TransferFunctionTab.cpp
    src/UI/TransferFunctionTab.h
    src/UI/DelayIrTab.cpp
    src/UI/DelayIrTab.h
    src/UI/RtaSplTab.cpp
    src/UI/RtaSplTab.h
    src/UI/EQTab.cpp
    src/UI/EQTab.h
    src/UI/LimitTestTab.cpp
    src/UI/LimitTestTab.h
    src/UI/UtilitiesTab.cpp
    src/UI/UtilitiesTab.h
    src/UI/VenueTab.cpp
    src/UI/VenueTab.h
    src/UI/LiveModeOverlay.cpp
    src/UI/LiveModeOverlay.h
    src/UI/UiContext.h
    src/UI/LogFrequencyGraph.h
    src/UI/Theme.h
    src/UI/Buttons.h
    src/UI/Strings.h
    src/Core/Acoustics.h
    src/Core/Utilities.h
    src/Core/Log.cpp
    src/Core/Log.h
    src/Core/KeepAwake.h
    src/Core/ProjectStore.cpp
    src/Core/ProjectStore.h
    src/Core/Exporters.cpp
    src/Core/Exporters.h
    src/Core/SystemCorrection.h
    src/Core/MicPermission.h
)

if(APPLE)
    # マイク許可の実装は macOS のみ Objective-C++（その他プラットフォームはスタブ）
    target_sources(EasyEQAssistant PRIVATE src/Core/MicPermission.mm)
else()
    target_sources(EasyEQAssistant PRIVATE src/Core/MicPermissionStub.cpp)
endif()

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

if(APPLE)
    target_link_libraries(EasyEQAssistant PRIVATE "-framework AVFoundation" "-framework AVFAudio" "-framework IOKit")

    # バンドル全体を ad-hoc 署名する（リンカの署名は実行ファイルのみのため）。
    # 未署名バンドルでは TCC がダイアログを出さない場合がある
    add_custom_command(TARGET EasyEQAssistant POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:EasyEQAssistant>"
        VERBATIM)
endif()

# ---- ASIO（Windows・任意）----
# 規約により ASIO SDK はリポジトリにコミットできない。libs/asio_sdk/ に展開して
# ビルドすると ASIO 対応が有効になる（ない場合は WASAPI で動く。仕様 6.1）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio_sdk/common/asio.cpp")
    message(STATUS "ASIO SDK found: enabling ASIO support")
    target_include_directories(EasyEQAssistant PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio_sdk")
    target_compile_definitions(EasyEQAssistant PRIVATE JUCE_ASIO=1)
endif()

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
        tests/TransferFunctionTests.cpp
        tests/DelayEstimatorTests.cpp
        tests/ImpulseResponseTests.cpp
        tests/PEQTests.cpp
        tests/EQProposerTests.cpp
        tests/RtaSplTests.cpp
        tests/UtilitiesTests.cpp
        tests/AudioTests.cpp
        tests/ThdTests.cpp
        tests/EndToEndTests.cpp
        tests/TransferFunctionRegressionTests.cpp
        src/DSP/TransferFunctionAccumulator.cpp
        src/DSP/DelayEstimator.cpp
        src/DSP/ImpulseResponseBuilder.cpp
        src/DSP/EQProposer.cpp
        src/DSP/RtaSplAnalyzer.cpp
    )
    target_include_directories(EasyEQTests PRIVATE src)
    target_compile_definitions(EasyEQTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(EasyEQTests PRIVATE
        gtest_main
        juce::juce_dsp
        # Core/JuceModules.h 経由でGUIモジュールの静的初期化子も参照されるためリンクする
        juce::juce_graphics
        juce::juce_gui_basics
    )

    include(GoogleTest)
    gtest_discover_tests(EasyEQTests)
endif()
