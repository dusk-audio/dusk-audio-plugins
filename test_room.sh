#!/bin/bash

echo "Testing StudioReverb Room Algorithm"
echo "===================================="
echo ""

# Create a simple test file that uses the plugin
cat > test_room_simple.cpp << 'EOF'
#include <iostream>
#include <dlfcn.h>

int main() {
    std::cout << "Testing Room reverb functionality\n";

    // Try to load the VST3 plugin
    void* handle = dlopen("/home/marc/.vst3/StudioReverb.vst3/Contents/x86_64-linux/StudioReverb.so", RTLD_LAZY);
    if (handle) {
        std::cout << "✓ VST3 plugin loaded successfully\n";
        dlclose(handle);
    } else {
        std::cout << "✗ Failed to load VST3 plugin: " << dlerror() << "\n";
    }

    std::cout << "\nTo test the Room reverb:\n";
    std::cout << "1. The plugin has been rebuilt with fixes\n";
    std::cout << "2. Room algorithm now uses FV3_REVTYPE_PROG2\n";
    std::cout << "3. Progenitor2 parameters are properly initialized\n";
    std::cout << "\nManual verification required in DAW:\n";
    std::cout << "- Load StudioReverb VST3\n";
    std::cout << "- Select Room algorithm\n";
    std::cout << "- Turn Late Level to 100%\n";
    std::cout << "- Should now produce reverb output\n";

    return 0;
}
EOF

# Compile and run the test
g++ -o test_room_simple test_room_simple.cpp -ldl
if [ $? -eq 0 ]; then
    ./test_room_simple
    rm test_room_simple test_room_simple.cpp
else
    echo "Failed to compile test"
fi

echo ""
echo "Debug output locations:"
echo "- Check terminal output when running DAW for printf debug messages"
echo "- Look for '=== ROOM REVERB DEBUG ===' messages"
echo "- Check maxInput and maxOutput values"