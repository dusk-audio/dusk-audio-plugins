// Find which parameter causes progenitor2 to fail
#include <iostream>
#include <cmath>
#include <cstring>

#define LIBFV3_FLOAT
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

void testConfig(const char* name, fv3::progenitor2_f& reverb) {
    const int blockSize = 512;
    
    float* left = new float[blockSize];
    float* right = new float[blockSize];
    
    // Impulse
    std::memset(left, 0, blockSize * sizeof(float));
    std::memset(right, 0, blockSize * sizeof(float));
    left[0] = 1.0f;
    right[0] = 1.0f;
    
    reverb.processreplace(left, right, left, right, blockSize);
    
    float maxOut = 0;
    for (int i = 0; i < blockSize; i++) {
        maxOut = std::max(maxOut, std::abs(left[i]) + std::abs(right[i]));
    }
    
    std::cout << name << ": maxOut=" << maxOut;
    if (maxOut > 0.001f) {
        std::cout << " ✅ WORKS" << std::endl;
    } else {
        std::cout << " ❌ FAILS" << std::endl;
    }
    
    delete[] left;
    delete[] right;
}

int main() {
    const float sampleRate = 48000.0f;
    
    std::cout << "Testing different progenitor2 configurations...\n" << std::endl;
    
    // Test 1: Basic init (what worked before)
    {
        fv3::progenitor2_f reverb;
        reverb.setSampleRate(sampleRate);
        reverb.setMuteOnChange(false);
        reverb.setwet(0);
        reverb.setdryr(0);
        reverb.setwidth(0.9f);
        reverb.setrt60(0.7f);
        reverb.setidiffusion1(0.75f);
        reverb.setodiffusion1(0.625f);
        testConfig("Basic (wet=0)", reverb);
    }
    
    // Test 2: With wet=6 like JUCE
    {
        fv3::progenitor2_f reverb;
        reverb.setSampleRate(sampleRate);
        reverb.setMuteOnChange(false);
        reverb.setwet(6.0f);  // Changed
        reverb.setdryr(0);
        reverb.setwidth(0.9f);
        reverb.setrt60(0.7f);
        reverb.setidiffusion1(0.75f);
        reverb.setodiffusion1(0.625f);
        testConfig("With wet=6", reverb);
    }
    
    // Test 3: With RSFactor
    {
        fv3::progenitor2_f reverb;
        reverb.setSampleRate(sampleRate);
        reverb.setMuteOnChange(false);
        reverb.setwet(0);
        reverb.setdryr(0);
        reverb.setwidth(0.9f);
        reverb.setrt60(0.7f);
        reverb.setidiffusion1(0.75f);
        reverb.setodiffusion1(0.625f);
        reverb.setRSFactor(1.6f);  // Added
        testConfig("With RSFactor", reverb);
    }
    
    // Test 4: With damp
    {
        fv3::progenitor2_f reverb;
        reverb.setSampleRate(sampleRate);
        reverb.setMuteOnChange(false);
        reverb.setwet(0);
        reverb.setdryr(0);
        reverb.setwidth(0.9f);
        reverb.setrt60(0.7f);
        reverb.setidiffusion1(0.75f);
        reverb.setodiffusion1(0.625f);
        reverb.setdamp(9000.0f);  // Added
        testConfig("With damp", reverb);
    }
    
    // Test 5: With setPreDelay
    {
        fv3::progenitor2_f reverb;
        reverb.setSampleRate(sampleRate);
        reverb.setMuteOnChange(false);
        reverb.setwet(0);
        reverb.setdryr(0);
        reverb.setwidth(0.9f);
        reverb.setrt60(0.7f);
        reverb.setidiffusion1(0.75f);
        reverb.setodiffusion1(0.625f);
        reverb.setPreDelay(0);  // Added
        testConfig("With PreDelay", reverb);
    }
    
    // Test 6: All JUCE parameters
    {
        fv3::progenitor2_f reverb;
        reverb.setSampleRate(sampleRate);
        reverb.setMuteOnChange(false);
        reverb.setwet(6.0f);
        reverb.setdryr(0);
        reverb.setwidth(0.9f);
        reverb.setrt60(0.7f);
        reverb.setidiffusion1(0.75f);
        reverb.setodiffusion1(0.625f);
        reverb.setRSFactor(1.6f);
        reverb.setdamp(9000.0f);
        reverb.setPreDelay(0);
        testConfig("All JUCE params", reverb);
    }
    
    return 0;
}
