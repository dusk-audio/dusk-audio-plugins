#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <fstream>
#include <vector>

/**
 * MidiExporter - Export generated drum patterns to standard MIDI files
 *
 * Supports:
 * - Type 0 (single track) and Type 1 (multi-track) MIDI files
 * - Variable length encoding
 * - Tempo and time signature meta events
 * - Standard MIDI file format (SMF)
 */
class MidiExporter
{
public:
    MidiExporter() = default;
    ~MidiExporter() = default;

    /**
     * Export a MidiBuffer to a MIDI file
     * @param buffer The MIDI buffer to export
     * @param file Output file path
     * @param bpm Tempo in BPM
     * @param ppq Pulses per quarter note (ticks per beat)
     * @param bars Number of bars (for total length calculation)
     * @return true if export succeeded
     */
    static bool exportToFile(const juce::MidiBuffer& buffer,
                            const juce::File& file,
                            double bpm = 120.0,
                            int ppq = 960,
                            int bars = 4);

    /**
     * Export a MidiMessageSequence to a MIDI file
     * @param sequence The MIDI sequence to export
     * @param file Output file path
     * @param bpm Tempo in BPM
     * @param ppq Pulses per quarter note
     * @return true if export succeeded
     */
    static bool exportSequenceToFile(const juce::MidiMessageSequence& sequence,
                                     const juce::File& file,
                                     double bpm = 120.0,
                                     int ppq = 960);

    /**
     * Create a MidiFile object from a MidiBuffer
     * @param buffer The MIDI buffer
     * @param bpm Tempo
     * @param ppq Ticks per quarter note
     * @return MidiFile object
     */
    static juce::MidiFile createMidiFile(const juce::MidiBuffer& buffer,
                                         double bpm = 120.0,
                                         int ppq = 960);

    /**
     * Generate multiple bars and export to file
     * @param generator Function that generates MIDI for a bar range
     * @param file Output file
     * @param numBars Number of bars to generate
     * @param bpm Tempo
     * @param ppq Ticks per quarter note
     * @return true if export succeeded
     */
    template<typename GeneratorFunc>
    static bool exportGeneratedPattern(GeneratorFunc generator,
                                       const juce::File& file,
                                       int numBars,
                                       double bpm = 120.0,
                                       int ppq = 960)
    {
        juce::MidiMessageSequence sequence;

        // Add tempo meta event
        auto tempoEvent = juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / bpm));
        tempoEvent.setTimeStamp(0);
        sequence.addEvent(tempoEvent);

        // Add time signature (4/4)
        auto timeSigEvent = juce::MidiMessage::timeSignatureMetaEvent(4, 2); // 4/4, quarter note = 1 beat
        timeSigEvent.setTimeStamp(0);
        sequence.addEvent(timeSigEvent);

        // Generate and add MIDI for each bar
        for (int bar = 0; bar < numBars; ++bar)
        {
            juce::MidiBuffer barBuffer = generator(bar, 1, bpm);

            int tickOffset = bar * ppq * 4; // 4 beats per bar

            for (const auto metadata : barBuffer)
            {
                auto msg = metadata.getMessage();
                msg.setTimeStamp(msg.getTimeStamp() + tickOffset);
                sequence.addEvent(msg);
            }
        }

        // Add end of track
        auto endTrack = juce::MidiMessage::endOfTrack();
        endTrack.setTimeStamp(static_cast<double>(numBars * ppq * 4));
        sequence.addEvent(endTrack);

        sequence.sort();
        sequence.updateMatchedPairs();

        return exportSequenceToFile(sequence, file, bpm, ppq);
    }

private:
    // Helper to write variable length value
    static void writeVariableLength(std::vector<uint8_t>& data, unsigned int value);

    // Helper to write big-endian 16-bit value
    static void write16bit(std::vector<uint8_t>& data, uint16_t value);

    // Helper to write big-endian 32-bit value
    static void write32bit(std::vector<uint8_t>& data, uint32_t value);
};
