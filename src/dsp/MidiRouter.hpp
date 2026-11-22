#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace et5m {

enum class ManualId : int {
    Upper = 0,
    Lower = 1,
    Pedal = 2
};

struct MidiRoutingConfig {
    // Channel convention: 0 = Omni, 1..16 = specific MIDI channel, -1 = Off
    int upperChannel{1};
    int lowerChannel{2};
    int pedalChannel{3};
    int upperOctaveShift{0}; // -2..+2
    int lowerOctaveShift{0}; // -2..+2
    int pedalOctaveShift{0}; // -2..+2
    bool singleKeyboardSplit{false};
    int splitPointMidi{60};  // C4
    int pedalSplitTopMidi{47}; // B2
    bool pedalOctaveFold{true};
    bool clampToHardwareRange{true};
};

struct RoutedNoteEvent {
    ManualId manual;
    int midiNote;
    bool isNoteOn;
};

class MidiRouter {
public:
    static constexpr int kUpperMin = 53; // F3
    static constexpr int kUpperMax = 96; // C7
    static constexpr int kLowerMin = 36; // C2
    static constexpr int kLowerMax = 79; // G5
    static constexpr int kPedalMin = 36; // C2
    static constexpr int kPedalMax = 48; // C3

    void reset() noexcept {
        for (auto& ch : activeSlots_) {
            for (auto& slot : ch) {
                slot = {};
            }
        }
    }

    template <typename Callback>
    void handleNoteOn(int channel1to16, int rawMidiNote, const MidiRoutingConfig& cfg, Callback&& cb) noexcept {
        if (channel1to16 < 1 || channel1to16 > 16 || rawMidiNote < 0 || rawMidiNote > 127) {
            return;
        }
        auto& slot = activeSlots_[static_cast<std::size_t>(channel1to16 - 1)][static_cast<std::size_t>(rawMidiNote)];
        // Release any stale notes on this same channel+key before retriggering
        releaseSlot(slot, cb);

        if (cfg.singleKeyboardSplit) {
            if (rawMidiNote <= cfg.pedalSplitTopMidi) {
                int n = mapPedalNote(rawMidiNote + cfg.pedalOctaveShift * 12, cfg);
                if (n >= 0) {
                    slot.pedalActive = true;
                    slot.pedalNote = n;
                    cb(RoutedNoteEvent{ManualId::Pedal, n, true});
                }
            } else if (rawMidiNote < cfg.splitPointMidi) {
                int n = mapLowerNote(rawMidiNote + cfg.lowerOctaveShift * 12, cfg);
                if (n >= 0) {
                    slot.lowerActive = true;
                    slot.lowerNote = n;
                    cb(RoutedNoteEvent{ManualId::Lower, n, true});
                }
            } else {
                int n = mapUpperNote(rawMidiNote + cfg.upperOctaveShift * 12, cfg);
                if (n >= 0) {
                    slot.upperActive = true;
                    slot.upperNote = n;
                    cb(RoutedNoteEvent{ManualId::Upper, n, true});
                }
            }
            return;
        }

        if (matchesChannel(channel1to16, cfg.upperChannel)) {
            int n = mapUpperNote(rawMidiNote + cfg.upperOctaveShift * 12, cfg);
            if (n >= 0) {
                slot.upperActive = true;
                slot.upperNote = n;
                cb(RoutedNoteEvent{ManualId::Upper, n, true});
            }
        }
        if (matchesChannel(channel1to16, cfg.lowerChannel)) {
            int n = mapLowerNote(rawMidiNote + cfg.lowerOctaveShift * 12, cfg);
            if (n >= 0) {
                slot.lowerActive = true;
                slot.lowerNote = n;
                cb(RoutedNoteEvent{ManualId::Lower, n, true});
            }
        }
        if (matchesChannel(channel1to16, cfg.pedalChannel)) {
            int n = mapPedalNote(rawMidiNote + cfg.pedalOctaveShift * 12, cfg);
            if (n >= 0) {
                slot.pedalActive = true;
                slot.pedalNote = n;
                cb(RoutedNoteEvent{ManualId::Pedal, n, true});
            }
        }
    }

    template <typename Callback>
    void handleNoteOff(int channel1to16, int rawMidiNote, Callback&& cb) noexcept {
        if (channel1to16 < 1 || channel1to16 > 16 || rawMidiNote < 0 || rawMidiNote > 127) {
            return;
        }
        auto& slot = activeSlots_[static_cast<std::size_t>(channel1to16 - 1)][static_cast<std::size_t>(rawMidiNote)];
        releaseSlot(slot, cb);
    }

    template <typename Callback>
    void allNotesOff(Callback&& cb) noexcept {
        for (auto& ch : activeSlots_) {
            for (auto& slot : ch) {
                releaseSlot(slot, cb);
            }
        }
    }

private:
    struct ActiveSlot {
        bool upperActive{false};
        bool lowerActive{false};
        bool pedalActive{false};
        int upperNote{-1};
        int lowerNote{-1};
        int pedalNote{-1};
    };

    std::array<std::array<ActiveSlot, 128>, 16> activeSlots_{};

    [[nodiscard]] static bool matchesChannel(int ch1to16, int cfgCh) noexcept {
        return cfgCh == 0 || cfgCh == ch1to16;
    }

    [[nodiscard]] static int mapUpperNote(int note, const MidiRoutingConfig& cfg) noexcept {
        if (note < 36 || note > 96) {
            if (!cfg.clampToHardwareRange) return std::clamp(note, 36, 96);
        }
        if (cfg.clampToHardwareRange) {
            if (note < kUpperMin || note > kUpperMax) return -1;
            return note;
        }
        while (note < kUpperMin) note += 12;
        while (note > kUpperMax) note -= 12;
        return note;
    }

    [[nodiscard]] static int mapLowerNote(int note, const MidiRoutingConfig& cfg) noexcept {
        if (cfg.clampToHardwareRange) {
            if (note < kLowerMin || note > kLowerMax) return -1;
            return note;
        }
        while (note < kLowerMin) note += 12;
        while (note > kLowerMax) note -= 12;
        return note;
    }

    [[nodiscard]] static int mapPedalNote(int note, const MidiRoutingConfig& cfg) noexcept {
        if (cfg.pedalOctaveFold) {
            while (note < kPedalMin) note += 12;
            while (note > kPedalMax) note -= 12;
            return note;
        }
        if (cfg.clampToHardwareRange) {
            if (note < kPedalMin || note > kPedalMax) return -1;
            return note;
        }
        return std::clamp(note, kPedalMin, kPedalMax);
    }

    template <typename Callback>
    static void releaseSlot(ActiveSlot& slot, Callback&& cb) noexcept {
        if (slot.upperActive) {
            slot.upperActive = false;
            cb(RoutedNoteEvent{ManualId::Upper, slot.upperNote, false});
        }
        if (slot.lowerActive) {
            slot.lowerActive = false;
            cb(RoutedNoteEvent{ManualId::Lower, slot.lowerNote, false});
        }
        if (slot.pedalActive) {
            slot.pedalActive = false;
            cb(RoutedNoteEvent{ManualId::Pedal, slot.pedalNote, false});
        }
    }
};

} // namespace et5m
