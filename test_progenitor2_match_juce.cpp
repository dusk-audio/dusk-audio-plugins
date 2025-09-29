// Test progenitor2 with exact JUCE initialization
#include <iostream>
#include <cmath>
#include <cstring>

#define LIBFV3_FLOAT
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    std::cout << "Creating progenitor2_f to match JUCE setup..." << std::endl;
    fv3::progenitor2_f reverb;

    // Initialize EXACTLY like our JUCE plugin does
    reverb.setSampleRate(sampleRate);
    reverb.setMuteOnChange(false);
    reverb.setwet(6.0f);   // +6dB like in JUCE after prepare()
    reverb.setdryr(0);     // 0dB dry
    reverb.setwidth(0.9f); // Match JUCE value
    reverb.setrt60(0.7f);  // Match JUCE value
    reverb.setidiffusion1(0.75f);
    reverb.setodiffusion1(0.625f);
    // These calls are made in updateRoomReverb():
    reverb.setRSFactor(1.6f);  // size=16 -> 1.6
    reverb.setdamp(9000.0f);   // dampen parameter
    reverb.setPreDelay(0);      // preDelay

    std::cout << "Parameters: wet=" << reverb.getwet() << " dry=" << reverb.getdryr() 
              << " rt60=" << reverb.getrt60() << " damp=" << reverb.getdamp() << std::endl;

    // Test with small input like JUCE gets
    float* leftIn = new float[blockSize];
    float* rightIn = new float[blockSize];
    float* leftOut = new float[blockSize];
    float* rightOut = new float[blockSize];

    // Generate tiny signal like JUCE sees
    for (int i = 0; i < blockSize; i++) {
        leftIn[i] = 0.00001f * sin(2 * M_PI * 440.0f * i / sampleRate);
        rightIn[i] = 0.00001f * sin(2 * M_PI * 440.0f * i / sampleRate);
    }

    // Process like our fixed JUCE code
    std::memcpy(leftOut, leftIn, blockSize * sizeof(float));
    std::memcpy(rightOut, rightIn, blockSize * sizeof(float));
    reverb.processreplace(leftOut, rightOut, leftOut, rightOut, blockSize);

    // Check output
    float maxOut = 0;
    int nonZero = 0;
    for (int i = 0; i < blockSize; i++) {
        float mag = std::abs(leftOut[i]) + std::abs(rightOut[i]);
        if (mag > maxOut) maxOut = mag;
        if (mag > 0.0000001f) nonZero++;
    }

    std::cout << "Results: maxOut=" << maxOut << " nonZero=" << nonZero << std::endl;
    
    if (maxOut < 0.0000001f) {
        std::cout << "❌ NO OUTPUT - progenitor2 returns zeros!" << std::endl;
    } else {
        std::cout << "✅ SUCCESS - progenitor2 produces output!" << std::endl;
    }

    delete[] leftIn;
    delete[] rightIn;
    delete[] leftOut;
    delete[] rightOut;
    return 0;
}
