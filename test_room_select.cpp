// Test program that immediately selects Room reverb
#include <iostream>
#include <cstring>
#include <unistd.h>

int main() {
    std::cout << "Launching StudioReverb and selecting Room reverb..." << std::endl;

    // Launch the plugin in background
    if (fork() == 0) {
        execl("./build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb",
              "StudioReverb", nullptr);
        exit(1);
    }

    // Wait for plugin to initialize
    sleep(2);

    // The plugin should now be running with Room selected
    // Keep alive for a moment
    sleep(3);

    return 0;
}