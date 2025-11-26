#include "MidiExporter.h"

bool MidiExporter::exportToFile(const juce::MidiBuffer& buffer,
                                const juce::File& file,
                                double bpm,
                                int ppq,
                                int bars)
{
    juce::ignoreUnused(bars);

    // Create the MIDI file (end of track is already added in createMidiFile)
    juce::MidiFile midiFile = createMidiFile(buffer, bpm, ppq);

    // Write to file
    juce::FileOutputStream outputStream(file);

    if (!outputStream.openedOk())
        return false;

    return midiFile.writeTo(outputStream);
}

bool MidiExporter::exportSequenceToFile(const juce::MidiMessageSequence& sequence,
                                        const juce::File& file,
                                        double bpm,
                                        int ppq)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ppq);

    // Create a copy of the sequence to modify
    juce::MidiMessageSequence trackSequence(sequence);

    // Add tempo meta event at the beginning if not present
    bool hasTempoEvent = false;
    for (int i = 0; i < trackSequence.getNumEvents(); ++i)
    {
        if (trackSequence.getEventPointer(i)->message.isTempoMetaEvent())
        {
            hasTempoEvent = true;
            break;
        }
    }

    if (!hasTempoEvent)
    {
        auto tempoEvent = juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / bpm));
        tempoEvent.setTimeStamp(0);
        trackSequence.addEvent(tempoEvent, 0);
    }

    // Add time signature if not present
    bool hasTimeSig = false;
    for (int i = 0; i < trackSequence.getNumEvents(); ++i)
    {
        if (trackSequence.getEventPointer(i)->message.isTimeSignatureMetaEvent())
        {
            hasTimeSig = true;
            break;
        }
    }

    if (!hasTimeSig)
    {
        auto timeSigEvent = juce::MidiMessage::timeSignatureMetaEvent(4, 2);
        timeSigEvent.setTimeStamp(0);
        trackSequence.addEvent(timeSigEvent, 0);
    }

    // Make sure there's an end-of-track event
    bool hasEndOfTrack = false;
    double maxTime = 0;
    for (int i = 0; i < trackSequence.getNumEvents(); ++i)
    {
        auto* event = trackSequence.getEventPointer(i);
        maxTime = std::max(maxTime, event->message.getTimeStamp());

        if (event->message.isEndOfTrackMetaEvent())
            hasEndOfTrack = true;
    }

    if (!hasEndOfTrack)
    {
        auto endTrack = juce::MidiMessage::endOfTrack();
        endTrack.setTimeStamp(maxTime + 1);
        trackSequence.addEvent(endTrack);
    }

    trackSequence.sort();
    midiFile.addTrack(trackSequence);

    // Write to file
    juce::FileOutputStream outputStream(file);

    if (!outputStream.openedOk())
        return false;

    return midiFile.writeTo(outputStream);
}

juce::MidiFile MidiExporter::createMidiFile(const juce::MidiBuffer& buffer,
                                            double bpm,
                                            int ppq)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ppq);

    juce::MidiMessageSequence sequence;

    // Add tempo meta event
    auto tempoEvent = juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / bpm));
    tempoEvent.setTimeStamp(0);
    sequence.addEvent(tempoEvent);

    // Add time signature (4/4)
    auto timeSigEvent = juce::MidiMessage::timeSignatureMetaEvent(4, 2);
    timeSigEvent.setTimeStamp(0);
    sequence.addEvent(timeSigEvent);

    // Add track name
    auto trackName = juce::MidiMessage::textMetaEvent(3, "DrummerClone Drums");
    trackName.setTimeStamp(0);
    sequence.addEvent(trackName);

    // Copy events from buffer
    double maxTime = 0;
    for (const auto metadata : buffer)
    {
        auto msg = metadata.getMessage();
        // Use the timestamp from the message, not sample position
        double timestamp = msg.getTimeStamp();
        maxTime = std::max(maxTime, timestamp);
        sequence.addEvent(msg);
    }

    // Add end of track
    auto endTrack = juce::MidiMessage::endOfTrack();
    endTrack.setTimeStamp(maxTime + ppq);  // Add a little padding
    sequence.addEvent(endTrack);

    sequence.sort();
    sequence.updateMatchedPairs();

    midiFile.addTrack(sequence);

    return midiFile;
}

void MidiExporter::writeVariableLength(std::vector<uint8_t>& data, unsigned int value)
{
    // Variable length encoding: 7 bits per byte, MSB indicates continuation
    std::vector<uint8_t> bytes;

    bytes.push_back(static_cast<uint8_t>(value & 0x7F));
    value >>= 7;

    while (value > 0)
    {
        bytes.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }

    // Write in reverse order (big-endian style)
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
    {
        data.push_back(*it);
    }
}

void MidiExporter::write16bit(std::vector<uint8_t>& data, uint16_t value)
{
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(value & 0xFF));
}

void MidiExporter::write32bit(std::vector<uint8_t>& data, uint32_t value)
{
    data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(value & 0xFF));
}
