#include "ChordRecorder.h"

ChordRecorder::ChordRecorder() = default;

void ChordRecorder::startRecording()
{
    if (isRecording()) return;
    Base::startRecording(juce::Time::getCurrentTime().toMilliseconds());
}
