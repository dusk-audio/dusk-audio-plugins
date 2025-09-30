// Test exact JUCE scenario
#include <iostream>
#include <cstring>
#include <cmath>

#define LIBFV3_FLOAT
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

class TestReverb {
public:
    fv3::progenitor2_f room;
    float lateInBuffer[2][512];
    float lateOutBuffer[2][512];

    void init() {
        // Constructor initialization
        float defaultSampleRate = 48000.0f;
        room.setSampleRate(defaultSampleRate);
        room.setMuteOnChange(false);
        room.setwet(0);
        room.setdryr(0);
        room.setwidth(1.0);
        room.setrt60(2.0f);
        room.setidiffusion1(0.75f);
        room.setodiffusion1(0.75f);
    }

    void prepare(float sampleRate) {
        // Called by DAW when starting
        room.setSampleRate(sampleRate);
        updateRoom();
    }

    void updateRoom() {
        // Called by prepare and parameter changes
        room.setMuteOnChange(false);
        room.setwet(0);
        room.setdryr(0);
        room.setwidth(0.9f);
        room.setrt60(0.7f);
        room.setidiffusion1(0.75f);
        room.setodiffusion1(0.625f);
    }

    void process(int numSamples) {
        // Process exactly like JUCE
        room.processreplace(
            lateInBuffer[0],
            lateInBuffer[1],
            lateOutBuffer[0],
            lateOutBuffer[1],
            numSamples
        );
    }
};

int main() {
    TestReverb test;
    
    std::cout << "Testing exact JUCE scenario..." << std::endl;
    
    // Constructor
    test.init();
    
    // DAW calls prepare
    test.prepare(48000.0f);
    
    // Generate test signal
    for (int i = 0; i < 512; i++) {
        test.lateInBuffer[0][i] = 0;
        test.lateInBuffer[1][i] = 0;
    }
    test.lateInBuffer[0][0] = 1.0f;  // Impulse
    test.lateInBuffer[1][0] = 1.0f;
    
    // Process
    test.process(512);
    
    // Check output
    float maxOut = 0;
    for (int i = 0; i < 512; i++) {
        maxOut = std::max(maxOut, 
                         std::abs(test.lateOutBuffer[0][i]) + 
                         std::abs(test.lateOutBuffer[1][i]));
    }
    
    std::cout << "Output max: " << maxOut << std::endl;
    if (maxOut > 0.0001f) {
        std::cout << "✅ SUCCESS!" << std::endl;
    } else {
        std::cout << "❌ FAILED - no output!" << std::endl;
    }
    
    return 0;
}
