#pragma once

#include "FactoryPresets.hpp"
#include "PluginParams.hpp"
#include "dsp/ET5MEngine.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace et5m::plugin {

class ET5MAudioProcessor final : public juce::AudioProcessor {
public:
    ET5MAudioProcessor();
    ~ET5MAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }

    float mainBusRms() const noexcept { return mainRms_.load(std::memory_order_relaxed); }
    float cabinetBusRms() const noexcept { return cabRms_.load(std::memory_order_relaxed); }
    float directBusRms() const noexcept { return dirRms_.load(std::memory_order_relaxed); }
    float lfoRateHz() const noexcept { return lfoRateHz_.load(std::memory_order_relaxed); }
    int activeUpperNotes() const noexcept { return upperActiveNotes_.load(std::memory_order_relaxed); }
    int activeLowerNotes() const noexcept { return lowerActiveNotes_.load(std::memory_order_relaxed); }
    int activePedalMidi() const noexcept { return pedalActiveMidi_.load(std::memory_order_relaxed); }

private:
    void syncEngineState(const EngineState& baseState, double refA4Hz) noexcept;
    void handleMidiController(int channel, int ccNumber, int value, const MidiRoutingConfig& midiCfg) noexcept;
    void renderChunk(int startSample, int numSamples, juce::AudioBuffer<float>& buffer) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    CachedParameterPointers rawParams_{};
    int currentProgram_{0};

    ET5MEngine engine_{};

    double cc11Expression_{1.0};
    double cc7Volume_{1.0};
    bool cc64UpperSustain_{false};
    bool cc64PedalSustain_{false};

    std::vector<double> mixL_;
    std::vector<double> mixR_;
    std::vector<double> dirL_;
    std::vector<double> dirR_;
    std::vector<double> cabL_;
    std::vector<double> cabR_;

    double sumSqMix_{0.0};
    double sumSqCab_{0.0};
    double sumSqDir_{0.0};

    std::atomic<float> mainRms_{0.0f};
    std::atomic<float> cabRms_{0.0f};
    std::atomic<float> dirRms_{0.0f};
    std::atomic<float> lfoRateHz_{0.0f};
    std::atomic<int> upperActiveNotes_{0};
    std::atomic<int> lowerActiveNotes_{0};
    std::atomic<int> pedalActiveMidi_{-1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ET5MAudioProcessor)
};

} // namespace et5m::plugin
