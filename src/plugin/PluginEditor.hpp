#pragma once

#include "PluginProcessor.hpp"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace et5m::plugin {

enum class TabletInk { DarkCharcoal, CrimsonRed };

class IvoryRockerTablet final : public juce::Button {
public:
    IvoryRockerTablet(const juce::String& label, TabletInk ink = TabletInk::DarkCharcoal);
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    juce::String label_;
    TabletInk ink_;
};

class VintageConsoleLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    VintageConsoleLookAndFeel();
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
};

class TelemetryDisplay final : public juce::Component {
public:
    static juce::String formatVoiceText(int up, int lo, int pedMidi);
    void update(float mainRms);
    void paint(juce::Graphics& g) override;

private:
    float mainRms_{0.0f};
};

class ET5MAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ET5MAudioProcessorEditor(ET5MAudioProcessor& p);
    ~ET5MAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct TabletSlot {
        std::unique_ptr<IvoryRockerTablet> button;
        std::unique_ptr<ButtonAttachment> attachment;
        int group{0}; // 0=Effect/Pedal, 1=Lower, 2=Upper, 3=Sustain
    };

    struct KnobSlot {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<SliderAttachment> attachment;
        bool isBay{false};
    };

    struct ComboSlot {
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<ComboAttachment> attachment;
    };

    struct HSliderSlot {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<SliderAttachment> attachment;
        bool isOctaveRow{true};
    };

    struct ToggleSlot {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
        bool isLeftBay{true};
    };

    void addTablet(const char* paramId, const juce::String& title, TabletInk ink, int group);
    void addKnob(const char* paramId, const juce::String& title, bool isBay, const juce::String& suffix = {});
    void addCombo(const char* paramId, const juce::String& title, const juce::StringArray& items);
    void addHSlider(const char* paramId, const juce::String& title, bool isOctaveRow, bool formatMidiNote);
    void addToggle(const char* paramId, const juce::String& title, bool isLeftBay);
    void timerCallback() override;
    void syncPresetBox();

    ET5MAudioProcessor& proc_;
    VintageConsoleLookAndFeel consoleLook_;

    juce::ComboBox presetBox_;
    juce::TextButton prevPresetBtn_{"<"};
    juce::TextButton nextPresetBtn_{">"};
    juce::TextButton allOffBtn_{"ALL OFF"};
    TelemetryDisplay telemetry_;

    std::vector<TabletSlot> tablets_;
    std::vector<KnobSlot> knobs_;
    std::vector<ComboSlot> combos_;
    std::vector<HSliderSlot> hSliders_;
    std::vector<ToggleSlot> toggles_;

    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> tabletRailBounds_;
    juce::Rectangle<int> groupEffectPedalBounds_;
    juce::Rectangle<int> groupLowerBounds_;
    juce::Rectangle<int> groupUpperBounds_;
    std::array<juce::Rectangle<int>, 4> slotPocketBounds_{};
    juce::Rectangle<int> cheekRailBounds_;
    juce::Rectangle<int> leftCheekBounds_;
    juce::Rectangle<int> rightCheekBounds_;
    juce::Rectangle<int> bayBounds_;
    juce::Rectangle<int> leftCardBounds_;
    juce::Rectangle<int> rightCardBounds_;
    std::array<juce::Rectangle<int>, 3> manualRowBounds_{};
    std::array<juce::Rectangle<int>, 3> manualStatusBounds_{};
    std::array<bool, 3> manualActive_{false, false, false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ET5MAudioProcessorEditor)
};

} // namespace et5m::plugin
