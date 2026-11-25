#pragma once

// プロジェクト共通の JUCE モジュール include。
//
// 生成される JuceHeader.h は「using namespace juce;」を含むため、その後に
// Apple のヘッダ（MacTypes.h の Point など）を読むと名前衝突でコンパイルエラーに
// なる。そこで JuceHeader.h は使わず、必要なモジュールを直接 include する
//（using 指しのないモジュールヘッダだけなら安全）。

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
