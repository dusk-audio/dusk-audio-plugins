// Minimal VST3 plugin without JUCE for testing
#include <cstring>
#include <cmath>

extern "C" {
    // VST3 SDK minimal implementation would go here
    // This is a placeholder to show the structure

    struct AudioEffect {
        float sampleRate;
        int blockSize;
    };

    AudioEffect* createEffect() {
        return new AudioEffect{44100.0f, 512};
    }

    void processAudio(AudioEffect* effect, float** inputs, float** outputs, int numSamples) {
        // Simple pass-through
        for (int i = 0; i < numSamples; i++) {
            outputs[0][i] = inputs[0][i];
            outputs[1][i] = inputs[1][i];
        }
    }

    void deleteEffect(AudioEffect* effect) {
        delete effect;
    }
}